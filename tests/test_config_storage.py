"""Unit and integration tests for SmokerConfig validation and persistence."""

from __future__ import annotations

import json
from pathlib import Path
import pytest
from esp32_fan_control.Adapters.json_config_adapter import JsonConfigAdapter
from esp32_fan_control.Domain.configuration import SmokerConfig
from esp32_fan_control.Domain.temperature import SensorFault, SensorRole, TemperatureReading
from esp32_fan_control.Services.Ports.actuator_ports import BlowerActuatorPort, DamperActuatorPort
from esp32_fan_control.Services.Ports.sensor_port import TemperatureSensorPort
from esp32_fan_control.Services.smoker_control_service import SmokerControlService


class DummySensor(TemperatureSensorPort):
    def read_temperature(self, role: SensorRole) -> TemperatureReading:
        return TemperatureReading.from_fahrenheit(225.0, role, 1.0, fault=SensorFault.OK)


class DummyActuator(DamperActuatorPort, BlowerActuatorPort):
    def __init__(self) -> None:
        self.p = 0.0
        self.s = 0.0

    def set_position(self, pos: float) -> None:
        self.p = pos

    def set_speed(self, spd: float) -> None:
        self.s = spd


def test_smoker_config_validation_invariants() -> None:
    # Default is valid
    cfg = SmokerConfig()
    assert cfg.setpoint_f == 225.0

    # Low setpoint invalid
    with pytest.raises(ValueError):
        SmokerConfig(setpoint_f=50.0)

    # High setpoint invalid
    with pytest.raises(ValueError):
        SmokerConfig(setpoint_f=500.0)

    # Negative PID invalid
    with pytest.raises(ValueError):
        SmokerConfig(pid_kp=-1.0)

    # Invalid airflow threshold
    with pytest.raises(ValueError):
        SmokerConfig(airflow_threshold_pct=5.0)


def test_json_config_adapter_roundtrip(tmp_path: Path) -> None:
    cfg_file = tmp_path / "config.json"
    adapter = JsonConfigAdapter(cfg_file)

    # Non-existent file returns None
    assert adapter.load_config() is None

    # Save and reload
    cfg = SmokerConfig(setpoint_f=275.0, pid_kp=4.5, airflow_threshold_pct=35.0)
    assert adapter.save_config(cfg) is True
    assert cfg_file.is_file()

    loaded = adapter.load_config()
    assert loaded is not None
    assert loaded.setpoint_f == 275.0
    assert loaded.pid_kp == 4.5
    assert loaded.airflow_threshold_pct == 35.0


def test_json_config_adapter_corrupt_fallback(tmp_path: Path) -> None:
    cfg_file = tmp_path / "corrupt.json"
    cfg_file.write_text("{invalid json", encoding="utf-8")

    adapter = JsonConfigAdapter(cfg_file)
    assert adapter.load_config() is None


def test_service_persistence_workflow(tmp_path: Path) -> None:
    cfg_file = tmp_path / "test_persist.json"
    adapter = JsonConfigAdapter(cfg_file)

    sensor = DummySensor()
    actuator = DummyActuator()

    # 1. Fresh service starts at default 225
    service1 = SmokerControlService(
        sensor_port=sensor,
        damper_port=actuator,
        blower_port=actuator,
        config_storage=adapter,
    )
    assert service1.setpoint_f == 225.0

    # 2. Pitmaster updates setpoint to 250°F
    service1.setpoint_f = 250.0
    assert service1.setpoint_f == 250.0
    assert cfg_file.is_file()

    # 3. Simulate power loss / restart: new service instance with same storage adapter
    service2 = SmokerControlService(
        sensor_port=sensor,
        damper_port=actuator,
        blower_port=actuator,
        config_storage=adapter,
    )
    # Target setpoint MUST persist across reboot!
    assert service2.setpoint_f == 250.0
    assert service2.config.setpoint_f == 250.0
