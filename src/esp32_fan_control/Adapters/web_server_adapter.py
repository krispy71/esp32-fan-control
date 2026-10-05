"""Authenticated HTTPS boundary over copied state and a bounded control channel."""
from __future__ import annotations

import base64
from collections import OrderedDict
from dataclasses import asdict, replace
import hashlib
import hmac
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import math
from pathlib import Path
import re
import ssl
import threading
from urllib.parse import parse_qs, urlsplit

from esp32_fan_control.Domain.configuration import MeatProbeMode
from esp32_fan_control.Domain.control import ControlCommand, ControlCommandKind
from esp32_fan_control.Services.Ports.control_channel_port import ControlChannelPort

_MAX_BODY = 2048
_MODES = list(MeatProbeMode)
_CONFIG_FIELDS = {
    "setpoint_f", "pid_kp", "pid_ki", "pid_kd", "airflow_threshold_pct",
    "lid_drop_threshold_deg", "lid_pause_duration_s", "servo_min_pulse_us",
    "servo_max_pulse_us", "servo_inverted", "meat_probe_mode", "meater_cloud_token",
    "meater_mac_filter", "config_version",
}


def _object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("Duplicate field")
        result[key] = value
    return result


def _json_object(raw: bytes) -> dict:
    # Reject deep nesting before invoking the recursive decoder.
    depth = 0
    quoted = escaped = False
    for char in raw:
        if quoted:
            if escaped:
                escaped = False
            elif char == 92:
                escaped = True
            elif char == 34:
                quoted = False
        elif char == 34:
            quoted = True
        elif char in (123, 91):
            depth += 1
            if depth > 2:
                raise ValueError("Nested input is not supported")
        elif char in (125, 93):
            depth -= 1
    value = json.loads(raw, object_pairs_hook=_object, parse_constant=lambda _: (_ for _ in ()).throw(ValueError("Invalid number")))
    if not isinstance(value, dict):
        raise ValueError("Expected an object")
    return value


class _BoundedHTTPServer(ThreadingHTTPServer):
    daemon_threads = True
    request_queue_size = 4

    def __init__(self, *args, tls_context, **kwargs):
        self._slots = threading.BoundedSemaphore(4)
        self._tls_context = tls_context
        super().__init__(*args, **kwargs)

    def process_request(self, request, client_address):
        if not self._slots.acquire(blocking=False):
            request.close()
            return
        try:
            super().process_request(request, client_address)
        except Exception:
            self._slots.release()
            raise

    def process_request_thread(self, request, client_address):
        try:
            request.settimeout(3)
            secure = self._tls_context.wrap_socket(request, server_side=True)
            super().process_request_thread(secure, client_address)
        except (ssl.SSLError, OSError):
            request.close()
        finally:
            self._slots.release()

    def handle_error(self, request, client_address):
        # Malformed client input and disconnects never log credentials or payloads.
        pass


class WebServerAdapter:
    def __init__(self, channel: ControlChannelPort, static_dir: Path | str | None = None,
                 host: str = "127.0.0.1", port: int = 8443, access_dir: Path | str | None = None):
        self._channel = channel
        self._host, self._port = host, port
        self._access_dir = Path(access_dir) if access_dir else None
        self._static_dir = Path(static_dir) if static_dir else Path(__file__).resolve().parents[3] / "firmware/data"
        self._server = None
        self._thread = None
        self._lock = threading.Lock()
        self._pending = None
        self._next_id = 1
        self._results: OrderedDict[int, dict] = OrderedDict()
        self._access = None

    @property
    def is_running(self):
        return self._server is not None

    @property
    def port(self):
        return self._server.server_port if self._server else self._port

    def _state(self):
        state = self._channel.snapshot()
        if state is None:
            raise RuntimeError("Controller is initializing")
        return state

    def get_telemetry_dict(self):
        state = self._state()
        data = asdict(state.telemetry) if state.telemetry else {}
        if state.telemetry:
            data["timestamp_ms"] = int(data["timestamp_s"] * 1000)
            data["is_pit_valid"] = data["pit_temp_f"] is not None
            data["is_meat_valid"] = data["meat_temp_f"] is not None
        data["config_version"] = state.config_version
        return data

    def _config(self):
        state = self._state()
        data = asdict(state.config)
        data["meater_cloud_token_configured"] = bool(data.pop("meater_cloud_token"))
        data["meat_probe_mode"] = _MODES.index(state.config.meat_probe_mode)
        data["config_version"] = state.config_version
        data["persistence"] = state.persistence.value
        return data

    def _collect_result(self, state):
        if self._pending == state.last_command_id:
            self._results[self._pending] = {
                "request_id": self._pending, "status": "applied" if state.last_command_accepted else "rejected",
                "persistence": state.persistence.value, "config_version": state.config_version,
            }
            self._pending = None
            while len(self._results) > 8:
                self._results.popitem(last=False)

    def _submit(self, path, data):
        with self._lock:
            state = self._state()
            self._collect_result(state)
            if self._pending is not None:
                return 409, {"error": "A command is still pending; wait for its result"}
            version = data.get("config_version")
            if type(version) is not int or version < 0 or version > 0xFFFFFFFF:
                raise ValueError("A valid config_version is required")
            if version != state.config_version:
                return 409, {"error": "Configuration changed; reload settings before saving"}
            kw = {"request_id": self._next_id, "expected_config_version": version}
            if path == "/api/setpoint":
                if set(data) != {"setpoint", "config_version"} or type(data["setpoint"]) not in (float, int):
                    raise ValueError("Expected a numeric setpoint")
                if not math.isfinite(data["setpoint"]) or not 100 <= data["setpoint"] <= 450:
                    raise ValueError("Setpoint must be between 100 and 450 F")
                command = ControlCommand(ControlCommandKind.SET_SETPOINT, setpoint_f=data["setpoint"], **kw)
            elif path == "/api/lid-pause":
                if set(data) != {"action", "config_version"} or data["action"] not in ("pause", "resume"):
                    raise ValueError("Expected pause or resume")
                kind = ControlCommandKind.TRIGGER_LID_PAUSE if data["action"] == "pause" else ControlCommandKind.CANCEL_LID_PAUSE
                command = ControlCommand(kind, **kw)
            else:
                if not set(data) <= _CONFIG_FIELDS or len(data) < 2:
                    raise ValueError("Unknown or missing configuration fields")
                updates = {k: v for k, v in data.items() if k != "config_version"}
                for key, value in updates.items():
                    if key == "servo_inverted":
                        if type(value) is not bool:
                            raise ValueError("servo_inverted must be a boolean")
                    elif key in ("meater_cloud_token", "meater_mac_filter"):
                        limit = 95 if key == "meater_cloud_token" else 17
                        if not isinstance(value, str) or len(value) > limit or any(ord(c) < 33 or ord(c) > 126 for c in value):
                            raise ValueError("Invalid probe credential or address")
                        if key == "meater_mac_filter" and value and not re.fullmatch(r"(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}", value):
                            raise ValueError("Invalid probe address")
                    elif key == "meat_probe_mode":
                        if type(value) is not int or not 0 <= value < len(_MODES):
                            raise ValueError("Invalid probe mode")
                        updates[key] = _MODES[value]
                    elif type(value) not in (float, int) or not math.isfinite(value):
                        raise ValueError("Expected finite numeric parameters")
                    elif key.startswith("servo_") and type(value) is not int:
                        raise ValueError("Servo pulse limits must be whole microseconds")
                try:
                    config = replace(state.config, **updates)
                except (ValueError, TypeError):
                    raise ValueError("Invalid tuning or calibration limits") from None
                command = ControlCommand(ControlCommandKind.UPDATE_CONFIG, config=config, **kw)
            if not self._channel.submit(command):
                return 503, {"error": "Controller command queue is full"}
            self._pending = self._next_id
            self._next_id = self._next_id % 0xFFFFFFFF + 1
            return 202, {"status": "queued", "request_id": command.request_id}

    def _create_handler_class(self):
        adapter = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def _reply(self, status, value, content_type="application/json"):
                raw = json.dumps(value, allow_nan=False).encode() if content_type == "application/json" else value
                self.send_response(status)
                self.send_header("Content-Type", content_type)
                self.send_header("Content-Length", str(len(raw)))
                self.send_header("Cache-Control", "no-store")
                self.send_header("X-Content-Type-Options", "nosniff")
                self.send_header("X-Frame-Options", "DENY")
                if status == 401:
                    self.send_header("WWW-Authenticate", 'Basic realm="SmokerController", charset="UTF-8"')
                self.end_headers()
                self.wfile.write(raw)

            def _authorized(self, mutation=False):
                host = self.headers.get("Host", "")
                expected_hosts = {f"{h}:{adapter.port}" for h in adapter._access["hosts"]}
                if adapter.port == 443:
                    expected_hosts.update(adapter._access["hosts"])
                if len(self.headers.get_all("Host", [])) != 1 or host not in expected_hosts:
                    self._reply(403, {"error": "Invalid host"})
                    return False
                origin = self.headers.get("Origin")
                if (origin and origin != f"https://{host}") or (mutation and self.headers.get("Sec-Fetch-Site") == "cross-site"):
                    self._reply(403, {"error": "Cross-origin requests are not allowed"})
                    return False
                header = self.headers.get("Authorization", "")
                try:
                    if len(header) > 256 or not header.startswith("Basic "):
                        raise ValueError()
                    decoded = base64.b64decode(header[6:], validate=True).decode("ascii")
                    username, password = decoded.split(":", 1)
                    digest = hashlib.sha256(password.encode("ascii")).hexdigest()
                    valid = hmac.compare_digest(username, adapter._access["username"]) and hmac.compare_digest(digest, adapter._access["password_sha256"])
                except (ValueError, UnicodeError):
                    valid = False
                if not valid:
                    self._reply(401, {"error": "Authentication required"})
                return valid

            def do_GET(self):
                if not self._authorized():
                    return
                path = urlsplit(self.path).path
                try:
                    if path == "/api/telemetry":
                        self._reply(200, adapter.get_telemetry_dict())
                    elif path == "/api/config":
                        self._reply(200, adapter._config())
                    elif path == "/api/command":
                        values = parse_qs(urlsplit(self.path).query)
                        request_id = int(values.get("id", ["0"])[0])
                        with adapter._lock:
                            adapter._collect_result(adapter._state())
                            result = adapter._results.get(request_id)
                            if result:
                                self._reply(200, result)
                            elif adapter._pending == request_id:
                                self._reply(202, {"status": "queued", "request_id": request_id})
                            else:
                                self._reply(404, {"error": "Command result expired or unknown"})
                    else:
                        assets = {"/": ("index.html", "text/html; charset=utf-8"), "/style.css": ("style.css", "text/css; charset=utf-8"), "/app.js": ("app.js", "application/javascript; charset=utf-8")}
                        if path not in assets:
                            self._reply(404, {"error": "Not found"})
                            return
                        name, mime = assets[path]
                        self._reply(200, (adapter._static_dir / name).read_bytes(), mime)
                except (ValueError, RuntimeError, OSError):
                    self._reply(503, {"error": "Controller state or dashboard unavailable"})

            def do_POST(self):
                if not self._authorized(mutation=True):
                    return
                if self.path not in ("/api/setpoint", "/api/config", "/api/lid-pause"):
                    self._reply(404, {"error": "Not found"})
                    return
                try:
                    if self.headers.get("Transfer-Encoding") or len(self.headers.get_all("Content-Length", [])) != 1:
                        raise ValueError("A bounded Content-Length is required")
                    length = int(self.headers["Content-Length"])
                    if not 0 < length <= _MAX_BODY:
                        self._reply(413, {"error": "Request body too large or empty"})
                        return
                    if self.headers.get("Content-Type", "").split(";")[0] != "application/json":
                        self._reply(415, {"error": "Use application/json"})
                        return
                    raw = self.rfile.read(length)
                    if len(raw) != length:
                        raise ValueError("Incomplete request body")
                    data = _json_object(raw)
                    status, response = adapter._submit(self.path, data)
                    self._reply(status, response)
                except (ValueError, TypeError, UnicodeError, RecursionError):
                    self._reply(400, {"error": "Invalid request or configuration parameters"})
                except (RuntimeError, OSError):
                    self._reply(503, {"error": "Controller unavailable"})

        return Handler

    def start(self, background=True):
        if self._server:
            return
        if not self._access_dir:
            raise ValueError("HTTPS access provisioning is required; web remains disabled")
        for name in ("device-access.json", "device-key.pem"):
            if (self._access_dir / name).stat().st_mode & 0o077:
                raise ValueError("Private provisioning files must have mode 600")
        access_raw = (self._access_dir / "device-access.json").read_bytes()
        if len(access_raw) > _MAX_BODY:
            raise ValueError("Invalid access provisioning")
        access = _json_object(access_raw)
        if (access.get("username") != "admin" or not re.fullmatch(r"[0-9a-f]{64}", access.get("password_sha256", ""))
                or not isinstance(access.get("hosts"), list) or not 1 <= len(access["hosts"]) <= 8
                or any(not isinstance(h, str) or not re.fullmatch(r"[a-zA-Z0-9.-]{1,64}", h) for h in access["hosts"])):
            raise ValueError("Invalid access provisioning")
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.minimum_version = ssl.TLSVersion.TLSv1_2
        context.load_cert_chain(self._access_dir / "device-cert.pem", self._access_dir / "device-key.pem")
        self._access = access
        self._server = _BoundedHTTPServer((self._host, self._port), self._create_handler_class(), tls_context=context)
        if background:
            self._thread = threading.Thread(target=self._server.serve_forever, daemon=True)
            self._thread.start()
        else:
            self._server.serve_forever()

    def stop(self):
        if self._server:
            self._server.shutdown()
            self._server.server_close()
            if self._thread:
                self._thread.join(timeout=3)
            self._server = None
