"""
Web server adapter exposing local HTTP REST, SSE telemetry, and dashboard static files.
"""

from __future__ import annotations

import json
import mimetypes
import os
import threading
import time
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import TYPE_CHECKING, Any

if TYPE_CHECKING:
    from esp32_fan_control.Services.smoker_control_service import SmokerControlService


class WebServerAdapter:
    """
    HTTP server adapter that serves dashboard assets and REST/SSE endpoints.
    Enables local Wi-Fi / SoftAP control without external dependencies.
    """

    def __init__(
        self,
        service: SmokerControlService,
        static_dir: Path | str | None = None,
        host: str = "127.0.0.1",
        port: int = 8080,
    ) -> None:
        self._service = service
        self._host = host
        self._port = port
        self._server: ThreadingHTTPServer | None = None
        self._thread: threading.Thread | None = None
        self._is_running = False

        if static_dir is not None:
            self._static_dir = Path(static_dir).resolve()
        else:
            # Default to firmware/data relative to repository root
            repo_root = Path(__file__).resolve().parent.parent.parent.parent
            self._static_dir = (repo_root / "firmware" / "data").resolve()

    @property
    def is_running(self) -> bool:
        return self._is_running

    @property
    def port(self) -> int:
        if self._server:
            return self._server.server_port
        return self._port

    def get_telemetry_dict(self) -> dict[str, Any]:
        """Serialize current service telemetry to a standard dictionary."""
        snap = self._service.last_snapshot
        if snap is not None:
            return {
                "timestamp_s": snap.timestamp_s,
                "timestamp_ms": int(snap.timestamp_s * 1000),
                "pit_temp_f": snap.pit_temp_f,
                "meat_temp_f": snap.meat_temp_f,
                "setpoint_f": snap.setpoint_f,
                "damper_position_pct": snap.damper_position_pct,
                "blower_speed_pct": snap.blower_speed_pct,
                "demand_pct": snap.demand_pct,
                "is_pit_valid": snap.pit_temp_f is not None,
                "is_meat_valid": snap.meat_temp_f is not None,
                "lid_open": snap.lid_open,
                "status": snap.status,
            }
        return {
            "timestamp_s": time.time(),
            "timestamp_ms": int(time.time() * 1000),
            "pit_temp_f": None,
            "meat_temp_f": None,
            "setpoint_f": self._service.setpoint_f,
            "damper_position_pct": 0.0,
            "blower_speed_pct": 0.0,
            "demand_pct": 0.0,
            "is_pit_valid": False,
            "is_meat_valid": False,
            "lid_open": self._service.is_lid_open,
            "status": "INITIALIZING",
        }

    def _create_handler_class(self):
        adapter = self

        class RequestHandler(BaseHTTPRequestHandler):
            def log_message(self, format: str, *args: Any) -> None:
                # Suppress noisy access logs in production/tests
                pass

            def do_GET(self) -> None:
                path = self.path.split("?")[0]

                if path == "/api/telemetry":
                    self._handle_telemetry()
                elif path == "/api/events":
                    self._handle_events()
                else:
                    self._handle_static_file(path)

            def do_POST(self) -> None:
                path = self.path.split("?")[0]

                if path == "/api/setpoint":
                    self._handle_setpoint()
                elif path == "/api/lid-pause":
                    self._handle_lid_pause()
                else:
                    self.send_error(HTTPStatus.NOT_FOUND, "Endpoint not found")

            def _handle_telemetry(self) -> None:
                data = adapter.get_telemetry_dict()
                payload = json.dumps(data).encode("utf-8")
                self.send_response(HTTPStatus.OK)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(payload)))
                self.send_header("Access-Control-Allow-Origin", "*")
                self.end_headers()
                self.wfile.write(payload)

            def _handle_events(self) -> None:
                self.send_response(HTTPStatus.OK)
                self.send_header("Content-Type", "text/event-stream")
                self.send_header("Cache-Control", "no-cache")
                self.send_header("Connection", "keep-alive")
                self.send_header("Access-Control-Allow-Origin", "*")
                self.end_headers()

                while adapter._is_running:
                    try:
                        data = adapter.get_telemetry_dict()
                        line = f"data: {json.dumps(data)}\n\n"
                        self.wfile.write(line.encode("utf-8"))
                        self.wfile.flush()
                        time.sleep(1.0)
                    except (BrokenPipeError, ConnectionResetError):
                        break

            def _handle_setpoint(self) -> None:
                content_len = int(self.headers.get("Content-Length", 0))
                raw_body = self.rfile.read(content_len) if content_len > 0 else b"{}"

                try:
                    body = json.loads(raw_body.decode("utf-8"))
                    setpoint = float(body.get("setpoint", 0.0))
                except (ValueError, TypeError, json.JSONDecodeError):
                    self.send_error(HTTPStatus.BAD_REQUEST, "Invalid JSON or setpoint value")
                    return

                if not (100.0 <= setpoint <= 450.0):
                    self.send_error(HTTPStatus.BAD_REQUEST, "Setpoint out of valid range (100-450 F)")
                    return

                adapter._service.setpoint_f = setpoint
                resp = json.dumps({"status": "ok", "setpoint": setpoint}).encode("utf-8")
                self.send_response(HTTPStatus.OK)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(resp)))
                self.end_headers()
                self.wfile.write(resp)

            def _handle_lid_pause(self) -> None:
                content_len = int(self.headers.get("Content-Length", 0))
                action = "toggle"
                if content_len > 0:
                    try:
                        body = json.loads(self.rfile.read(content_len).decode("utf-8"))
                        action = body.get("action", "toggle")
                    except (ValueError, TypeError, json.JSONDecodeError):
                        pass

                if action == "pause":
                    adapter._service.trigger_lid_pause(time.time())
                elif action == "resume":
                    adapter._service.cancel_lid_pause()
                else:
                    # Toggle
                    if adapter._service.is_lid_open:
                        adapter._service.cancel_lid_pause()
                    else:
                        adapter._service.trigger_lid_pause(time.time())

                resp = json.dumps({
                    "status": "ok",
                    "lid_open": adapter._service.is_lid_open
                }).encode("utf-8")
                self.send_response(HTTPStatus.OK)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(resp)))
                self.end_headers()
                self.wfile.write(resp)

            def _handle_static_file(self, path: str) -> None:
                if path in ("/", ""):
                    path = "/index.html"

                # Sanitize path to prevent directory traversal
                cleaned_path = path.lstrip("/")
                target_file = (adapter._static_dir / cleaned_path).resolve()

                # Security check: must reside inside static_dir
                try:
                    target_file.relative_to(adapter._static_dir)
                except ValueError:
                    self.send_error(HTTPStatus.FORBIDDEN, "Forbidden")
                    return

                if not target_file.is_file():
                    self.send_error(HTTPStatus.NOT_FOUND, f"File not found: {path}")
                    return

                content_type, _ = mimetypes.guess_type(str(target_file))
                if content_type is None:
                    content_type = "application/octet-stream"
                if content_type.startswith("text/") or content_type == "application/javascript":
                    content_type += "; charset=utf-8"

                content = target_file.read_bytes()
                self.send_response(HTTPStatus.OK)
                self.send_header("Content-Type", content_type)
                self.send_header("Content-Length", str(len(content)))
                self.end_headers()
                self.wfile.write(content)

        return RequestHandler

    def start(self, background: bool = True) -> None:
        """Start the HTTP server."""
        if self._is_running:
            return

        handler_cls = self._create_handler_class()
        ports_to_try = [self._port] if self._port == 0 else [self._port + i for i in range(10)]
        bound = False
        last_err = None
        for p in ports_to_try:
            try:
                self._server = ThreadingHTTPServer((self._host, p), handler_cls)
                self._port = p
                bound = True
                break
            except OSError as err:
                last_err = err
                continue

        if not bound:
            assert last_err is not None
            raise last_err

        self._is_running = True

        if background:
            self._thread = threading.Thread(
                target=self._server.serve_forever,
                name="WebServerAdapterThread",
                daemon=True,
            )
            self._thread.start()
        else:
            try:
                self._server.serve_forever()
            finally:
                self._is_running = False

    def stop(self) -> None:
        """Stop the HTTP server."""
        if not self._is_running or not self._server:
            return

        self._is_running = False
        self._server.shutdown()
        self._server.server_close()
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=2.0)
        self._server = None
        self._thread = None
