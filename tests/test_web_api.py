"""Real HTTPS boundary tests: authorization, validation, queued state and persistence."""
import base64
import http.client
import json
import ssl
import subprocess
import sys
import time

import pytest

from esp32_fan_control.Adapters.control_channel_adapter import ThreadedControlChannel
from esp32_fan_control.Adapters.json_config_adapter import JsonConfigAdapter
from esp32_fan_control.Adapters.web_server_adapter import WebServerAdapter
from esp32_fan_control.Controller.cli import ConsoleActuator, SimulatedSensor
from esp32_fan_control.Domain.control import ControlCommand, ControlCommandKind
from esp32_fan_control.Services.smoker_control_service import SmokerControlService


@pytest.fixture(scope="session")
def access(tmp_path_factory):
    directory = tmp_path_factory.mktemp("https") / "device"
    subprocess.run([sys.executable, "tools/provision_device.py", "--output", str(directory)], check=True, capture_output=True)
    password = next(line.split(": ", 1)[1] for line in (directory / "operator-access.txt").read_text().splitlines() if line.startswith("HTTPS password:"))
    return directory, password


class RunningController:
    def __init__(self, directory, access, storage=None):
        self.channel = ThreadedControlChannel()
        self.actuator = ConsoleActuator()
        self.storage = storage or JsonConfigAdapter(directory / "settings.json")
        self.service = SmokerControlService(SimulatedSensor(), self.actuator, self.actuator,
                                           config_storage=self.storage, control_channel=self.channel)
        self.service.initialize()
        self.service.execute_cycle(0.0)
        self.server = WebServerAdapter(self.channel, port=0, access_dir=access[0])
        self.server.start()
        self.context = ssl.create_default_context(cafile=str(access[0] / "device-cert.pem"))
        self.auth = "Basic " + base64.b64encode(f"admin:{access[1]}".encode()).decode()
        self.origin = f"https://127.0.0.1:{self.server.port}"

    def request(self, path, body=None, authorized=True, headers=None, raw=None):
        connection = http.client.HTTPSConnection("127.0.0.1", self.server.port, context=self.context, timeout=5)
        combined = {"Authorization": self.auth} if authorized else {}
        if body is not None or raw is not None:
            combined["Content-Type"] = "application/json"
        combined.update(headers or {})
        payload = raw if raw is not None else json.dumps(body).encode() if body is not None else None
        connection.request("POST" if payload is not None else "GET", path, payload, combined)
        response = connection.getresponse()
        data = response.read()
        result = response.status, data, dict(response.getheaders())
        connection.close()
        return result

    def apply(self):
        self.service.execute_cycle(time.monotonic())

    def mutation(self, values, path="/api/config"):
        version = self.channel.snapshot().config_version
        return self.request(path, {"config_version": version, **values})


@pytest.fixture
def controller(tmp_path, access):
    app = RunningController(tmp_path, access)
    yield app
    app.server.stop()


def test_unprovisioned_and_untrusted_tls_fail_closed(tmp_path):
    server = WebServerAdapter(ThreadedControlChannel(), access_dir=tmp_path)
    with pytest.raises((ValueError, OSError)):
        server.start()
    assert not server.is_running


def test_auth_required_for_every_route_and_fixed_asset_allowlist(controller):
    for path in ("/", "/app.js", "/style.css", "/api/config", "/api/telemetry", "/api/command?id=1"):
        status, _, headers = controller.request(path, authorized=False)
        assert status == 401
        assert headers["WWW-Authenticate"].startswith("Basic")
    for path in ("/api/config", "/api/setpoint", "/api/lid-pause"):
        assert controller.request(path, {}, authorized=False)[0] == 401
    assert controller.request("/api/config", headers={"Authorization": "Basic invalid"})[0] == 401
    for path in ("/device-access.json", "/device-key.pem", "/../device-access.json", "/operator-access.txt"):
        assert controller.request(path)[0] == 404
    with pytest.raises(ssl.SSLCertVerificationError):
        connection = http.client.HTTPSConnection("127.0.0.1", controller.server.port, timeout=5)
        connection.request("GET", "/")


def test_origin_host_content_type_and_body_limits(controller):
    assert controller.mutation({"pid_kp": 5})[0] == 202
    # An unauthorized origin is rejected even with an otherwise valid credential.
    assert controller.request("/api/config", {"pid_kp": 9}, headers={"Origin": "https://attacker.example"})[0] == 403
    assert controller.request("/api/config", headers={"Host": "attacker.example"})[0] == 403
    assert controller.request("/api/config", {}, headers={"Sec-Fetch-Site": "cross-site"})[0] == 403
    assert controller.request("/api/config", {}, headers={"Content-Type": "text/plain"})[0] == 415
    assert controller.request("/api/config", raw=b" " * 2049)[0] == 413
    assert "Access-Control-Allow-Origin" not in controller.request("/api/config")[2]


@pytest.mark.parametrize("raw", [
    b'{"config_version":0,"pid_kp":1,"pid_kp":2}', b'{"config_version":0,"pid_kp":NaN}',
    b'{"config_version":0,"pid_kp":true}', b'{"config_version":0,"unknown":1}',
    b'{"config_version":0,"servo_inverted":"false"}', b'{"config_version":0,"servo_min_pulse_us":1000.5}',
    b'{"config_version":0,"servo_min_pulse_us":2400,"servo_max_pulse_us":1000}',
    b'{"config_version":0,"servo_max_pulse_us":2501}', b'{"config_version":0,"meat_probe_mode":9}',
    b'{"config_version":0,"meater_mac_filter":"not a mac"}', b'[]', b'{' + b'"x":[' * 30 + b'0' + b']' * 30 + b'}',
])
def test_invalid_inputs_do_not_acquire_control_authority(controller, raw):
    before = controller.channel.snapshot()
    assert controller.request("/api/config", raw=raw)[0] == 400
    controller.apply()
    assert controller.channel.snapshot().config == before.config


def test_calibration_queued_applied_persisted_and_restored(controller):
    status, raw, _ = controller.mutation({"servo_min_pulse_us": 800, "servo_max_pulse_us": 2200, "servo_inverted": True})
    assert status == 202
    command = json.loads(raw)
    assert controller.service.config.servo_min_pulse_us == 1000  # HTTP never mutates the owner.
    assert controller.request(f"/api/command?id={command['request_id']}")[0] == 202
    controller.apply()
    result = json.loads(controller.request(f"/api/command?id={command['request_id']}")[1])
    assert result["status"] == "applied" and result["persistence"] == "saved"
    assert controller.actuator.calibration.min_pulse_us == 800
    restored = SmokerControlService(SimulatedSensor(), ConsoleActuator(), ConsoleActuator(), config_storage=controller.storage)
    restored.initialize()
    assert restored.config.servo_inverted and restored.config.servo_max_pulse_us == 2200
    assert controller.storage.path.stat().st_mode & 0o077 == 0


def test_stale_updates_rejected_in_http_and_at_owner(controller):
    assert controller.mutation({"pid_kp": 5})[0] == 202
    controller.apply()
    assert controller.request("/api/config", {"config_version": 0, "pid_kp": 9})[0] == 409
    controller.channel.submit(ControlCommand(ControlCommandKind.SET_SETPOINT, setpoint_f=250, request_id=90))
    status, raw, _ = controller.mutation({"pid_kp": 9})
    assert status == 202
    controller.apply()
    assert controller.channel.snapshot().config.pid_kp == 5
    result = json.loads(controller.request(f"/api/command?id={json.loads(raw)['request_id']}")[1])
    assert result["status"] == "rejected"


def test_cloud_token_write_only_and_preserved(controller):
    # Generated test material is deliberately never printed or included in assertion output.
    import secrets
    token = secrets.token_urlsafe(24)
    assert controller.mutation({"meater_cloud_token": token})[0] == 202
    controller.apply()
    status, raw, headers = controller.request("/api/config")
    config = json.loads(raw)
    assert status == 200 and "meater_cloud_token" not in config
    assert config["meater_cloud_token_configured"] is True
    assert token.encode() not in raw
    assert headers["Cache-Control"] == "no-store"
    assert controller.mutation({"servo_min_pulse_us": 900})[0] == 202
    controller.apply()
    assert controller.service.config.meater_cloud_token == token
    assert controller.mutation({"meater_cloud_token": ""})[0] == 202
    controller.apply()
    assert controller.service.config.meater_cloud_token == ""


def test_storage_failure_is_reported_without_claiming_saved(tmp_path, access):
    class FailedStorage:
        def load_config(self): return None
        def save_config(self, config): return False
    app = RunningController(tmp_path, access, FailedStorage())
    try:
        status, raw, _ = app.mutation({"servo_min_pulse_us": 900})
        assert status == 202
        app.apply()
        result = json.loads(app.request(f"/api/command?id={json.loads(raw)['request_id']}")[1])
        assert result["status"] == "applied" and result["persistence"] == "failed"
    finally:
        app.server.stop()
