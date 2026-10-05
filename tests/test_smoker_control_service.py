"""Unit tests for SmokerControlService orchestration and fail-safe handling."""

from __future__ import annotations

from dataclasses import replace

import pytest

from esp32_fan_control.Domain.airflow import ActuatorCoordinator
from esp32_fan_control.Domain.pid import PIDConfig

from esp32_fan_control.Domain.configuration import DamperCalibration
from esp32_fan_control.Domain.temperature import (
    SensorFault,
    SensorRole,
    TemperatureReading,
)
from esp32_fan_control.Services.Ports.actuator_ports import (
    BlowerActuatorPort,
    DamperActuatorPort,
)
from esp32_fan_control.Services.Ports.sensor_port import TemperatureSensorPort
from esp32_fan_control.Services.Ports.telemetry_port import (
    TelemetryPublisherPort,
    TelemetrySnapshot,
)
from esp32_fan_control.Services.smoker_control_service import SmokerControlService


class FakeSensor(TemperatureSensorPort):
    def __init__(self, pit_f: float = 225.0, fault: SensorFault = SensorFault.OK) -> None:
        self.pit_f = pit_f
        self.fault = fault

    def read_temperature(self, role: SensorRole) -> TemperatureReading:
        if role == SensorRole.PIT:
            return TemperatureReading.from_fahrenheit(self.pit_f, role, 1.0, fault=self.fault)
        return TemperatureReading.from_fahrenheit(145.0, role, 1.0, fault=SensorFault.OK)


class FakeDamper(DamperActuatorPort):
    def __init__(self) -> None:
        self.last_position_pct: float = -1.0

    def configure(self, calibration: DamperCalibration) -> None:
        self.calibration = calibration

    def set_position(self, position_pct: float) -> None:
        self.last_position_pct = position_pct


class FakeBlower(BlowerActuatorPort):
    def __init__(self) -> None:
        self.last_speed_pct: float = -1.0

    def set_speed(self, speed_pct: float) -> None:
        self.last_speed_pct = speed_pct


class FakeTelemetry(TelemetryPublisherPort):
    def __init__(self) -> None:
        self.snapshots: list[TelemetrySnapshot] = []

    def publish(self, snapshot: TelemetrySnapshot) -> None:
        self.snapshots.append(snapshot)


def test_configuration_preserves_minimum_blower_speed() -> None:
    service = SmokerControlService(
        FakeSensor(pit_f=174), FakeDamper(), FakeBlower(),
        coordinator=ActuatorCoordinator(50, 50),
        pid_config=PIDConfig(kp=1, ki=0, kd=0),
    )
    service.initialize()
    assert service.execute_cycle(0).blower_speed_pct == pytest.approx(51)
    service.update_config(replace(service.config, airflow_threshold_pct=30))
    assert service.execute_cycle(1).blower_speed_pct == pytest.approx(65)


def test_service_fail_safe_on_sensor_disconnect() -> None:
    sensor = FakeSensor(pit_f=225.0, fault=SensorFault.DISCONNECTED)
    damper = FakeDamper()
    blower = FakeBlower()
    telemetry = FakeTelemetry()

    service = SmokerControlService(
        sensor_port=sensor,
        damper_port=damper,
        blower_port=blower,
        telemetry_port=telemetry,
        target_setpoint_f=225.0,
    )

    service.initialize()
    snapshot = service.execute_cycle(current_time_s=1.0)

    # Invariant: Disconnected sensor MUST force damper to 0% and blower to 0%
    assert service.is_fail_safe is True
    assert damper.last_position_pct == 0.0
    assert blower.last_speed_pct == 0.0
    assert snapshot.status.startswith("FAULT")
    assert len(telemetry.snapshots) == 1


def test_service_normal_closed_loop_regulation() -> None:
    # Pit is cold (210°F vs 225°F setpoint)
    sensor = FakeSensor(pit_f=210.0, fault=SensorFault.OK)
    damper = FakeDamper()
    blower = FakeBlower()
    telemetry = FakeTelemetry()

    service = SmokerControlService(
        sensor_port=sensor,
        damper_port=damper,
        blower_port=blower,
        telemetry_port=telemetry,
        target_setpoint_f=225.0,
    )

    service.initialize()
    snapshot = service.execute_cycle(current_time_s=1.0)

    assert service.is_fail_safe is False
    assert snapshot.pit_temp_f == 210.0
    assert snapshot.status == "REGULATING"
    # Damper must be open when pit is cold
    assert damper.last_position_pct > 0.0


def test_service_lid_open_detection_closes_actuators() -> None:
    sensor = FakeSensor(pit_f=225.0)
    damper = FakeDamper()
    blower = FakeBlower()

    service = SmokerControlService(
        sensor_port=sensor,
        damper_port=damper,
        blower_port=blower,
        target_setpoint_f=225.0,
    )

    service.initialize()

    # Cycle at normal temp
    service.execute_cycle(current_time_s=0.0)

    # Rapid temperature drop (e.g. lid opened, drops to 195°F)
    sensor.pit_f = 195.0
    service.initialize()
    snapshot = service.execute_cycle(current_time_s=10.0)

    assert snapshot.lid_open is True
    assert snapshot.status == "LID_OPEN"
    # Invariant: Lid open forces actuators to 0 to prevent flare-up
    assert damper.last_position_pct == 0.0
    assert blower.last_speed_pct == 0.0


def test_service_manual_lid_pause_and_last_snapshot() -> None:
    sensor = FakeSensor(pit_f=225.0)
    damper = FakeDamper()
    blower = FakeBlower()

    service = SmokerControlService(
        sensor_port=sensor,
        damper_port=damper,
        blower_port=blower,
        target_setpoint_f=225.0,
    )

    service.initialize()
    assert service.last_snapshot is None
    snap = service.execute_cycle(current_time_s=1.0)
    assert service.last_snapshot == snap

    # Manual pause
    service.trigger_lid_pause(current_time_s=2.0)
    assert service.is_lid_open is True
    snap2 = service.execute_cycle(current_time_s=2.0)
    assert snap2.lid_open is True
    assert damper.last_position_pct == 0.0

    # Manual cancel
    service.cancel_lid_pause()
    assert service.is_lid_open is False
    snap3 = service.execute_cycle(current_time_s=3.0)
    assert snap3.lid_open is False
