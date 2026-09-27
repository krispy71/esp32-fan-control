"""Tests for WebServerAdapter REST endpoints and static file serving."""

from __future__ import annotations

import json
import urllib.request
import urllib.error
import pytest
from esp32_fan_control.Adapters.web_server_adapter import WebServerAdapter
from esp32_fan_control.Domain.temperature import SensorFault, SensorRole, TemperatureReading
from esp32_fan_control.Services.Ports.actuator_ports import BlowerActuatorPort, DamperActuatorPort
from esp32_fan_control.Services.Ports.sensor_port import TemperatureSensorPort
from esp32_fan_control.Services.smoker_control_service import SmokerControlService


class DummySensor(TemperatureSensorPort):
    def __init__(self, pit_f: float = 225.0) -> None:
        self.pit_f = pit_f

    def read_temperature(self, role: SensorRole) -> TemperatureReading:
        if role == SensorRole.PIT:
            return TemperatureReading.from_fahrenheit(self.pit_f, role, 1.0, fault=SensorFault.OK)
        return TemperatureReading.from_fahrenheit(150.0, role, 1.0, fault=SensorFault.OK)


class DummyActuator(DamperActuatorPort, BlowerActuatorPort):
    def __init__(self) -> None:
        self.damper = 0.0
        self.blower = 0.0

    def set_position(self, p: float) -> None:
        self.damper = p

    def set_speed(self, s: float) -> None:
        self.blower = s


@pytest.fixture
def running_server():
    sensor = DummySensor(pit_f=224.5)
    actuator = DummyActuator()
    service = SmokerControlService(
        sensor_port=sensor,
        damper_port=actuator,
        blower_port=actuator,
        target_setpoint_f=225.0,
    )
    # Execute one cycle so there is an active snapshot
    service.execute_cycle(1.0)

    # Use port 0 to bind to an ephemeral OS-assigned port
    adapter = WebServerAdapter(service=service, host="127.0.0.1", port=0)
    adapter.start(background=True)
    try:
        yield adapter, service
    finally:
        adapter.stop()


def test_get_static_index(running_server):
    adapter, _ = running_server
    url = f"http://127.0.0.1:{adapter.port}/"
    with urllib.request.urlopen(url) as response:
        assert response.status == 200
        content_type = response.headers.get("Content-Type", "")
        assert "text/html" in content_type
        body = response.read().decode("utf-8")
        assert "<title>ESP32 Smoker Controller</title>" in body


def test_get_static_assets(running_server):
    adapter, _ = running_server
    # CSS
    url_css = f"http://127.0.0.1:{adapter.port}/style.css"
    with urllib.request.urlopen(url_css) as response:
        assert response.status == 200
        assert "text/css" in response.headers.get("Content-Type", "")
        css_body = response.read().decode("utf-8")
        assert "app-container" in css_body

    # JS
    url_js = f"http://127.0.0.1:{adapter.port}/app.js"
    with urllib.request.urlopen(url_js) as response:
        assert response.status == 200
        assert "javascript" in response.headers.get("Content-Type", "")
        js_body = response.read().decode("utf-8")
        assert "trendChart" in js_body


def test_get_telemetry_endpoint(running_server):
    adapter, service = running_server
    url = f"http://127.0.0.1:{adapter.port}/api/telemetry"
    with urllib.request.urlopen(url) as response:
        assert response.status == 200
        assert "application/json" in response.headers.get("Content-Type", "")
        data = json.loads(response.read().decode("utf-8"))

        assert data["pit_temp_f"] == 224.5
        assert data["setpoint_f"] == 225.0
        assert data["is_pit_valid"] is True
        assert data["lid_open"] is False
        assert "status" in data


def test_post_setpoint_endpoint(running_server):
    adapter, service = running_server
    url = f"http://127.0.0.1:{adapter.port}/api/setpoint"

    payload = json.dumps({"setpoint": 275.0}).encode("utf-8")
    req = urllib.request.Request(url, data=payload, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req) as response:
        assert response.status == 200
        resp_data = json.loads(response.read().decode("utf-8"))
        assert resp_data["status"] == "ok"
        assert resp_data["setpoint"] == 275.0
        assert service.setpoint_f == 275.0


def test_post_setpoint_validation(running_server):
    adapter, _ = running_server
    url = f"http://127.0.0.1:{adapter.port}/api/setpoint"

    # Out of range (< 100°F)
    payload_low = json.dumps({"setpoint": 50.0}).encode("utf-8")
    req_low = urllib.request.Request(url, data=payload_low, headers={"Content-Type": "application/json"})
    with pytest.raises(urllib.error.HTTPError) as exc_low:
        urllib.request.urlopen(req_low)
    assert exc_low.value.code == 400

    # Non-numeric
    payload_bad = json.dumps({"setpoint": "invalid"}).encode("utf-8")
    req_bad = urllib.request.Request(url, data=payload_bad, headers={"Content-Type": "application/json"})
    with pytest.raises(urllib.error.HTTPError) as exc_bad:
        urllib.request.urlopen(req_bad)
    assert exc_bad.value.code == 400


def test_post_lid_pause_endpoint(running_server):
    adapter, service = running_server
    url = f"http://127.0.0.1:{adapter.port}/api/lid-pause"

    # 1. Trigger pause
    req1 = urllib.request.Request(url, data=json.dumps({"action": "pause"}).encode("utf-8"), headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req1) as resp1:
        assert resp1.status == 200
        data1 = json.loads(resp1.read().decode("utf-8"))
        assert data1["lid_open"] is True
        assert service.is_lid_open is True

    # 2. Resume
    req2 = urllib.request.Request(url, data=json.dumps({"action": "resume"}).encode("utf-8"), headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req2) as resp2:
        assert resp2.status == 200
        data2 = json.loads(resp2.read().decode("utf-8"))
        assert data2["lid_open"] is False
        assert service.is_lid_open is False


def test_get_and_post_config_endpoint(running_server):
    adapter, service = running_server
    url = f"http://127.0.0.1:{adapter.port}/api/config"

    # GET config
    with urllib.request.urlopen(url) as resp:
        assert resp.status == 200
        cfg = json.loads(resp.read().decode("utf-8"))
        assert cfg["setpoint_f"] == 225.0
        assert cfg["pid_kp"] == 3.0

    # POST updated config
    payload = json.dumps({"setpoint_f": 260.0, "pid_kp": 5.0}).encode("utf-8")
    req = urllib.request.Request(url, data=payload, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req) as resp:
        assert resp.status == 200
        res_data = json.loads(resp.read().decode("utf-8"))
        assert res_data["status"] == "ok"
        assert service.setpoint_f == 260.0
        assert service.config.pid_kp == 5.0

