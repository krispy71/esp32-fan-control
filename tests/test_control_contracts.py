"""Core acceptance through production services with fakes only at external ports."""

import math
from collections import deque
from dataclasses import replace

import pytest

from esp32_fan_control.Domain.configuration import (
    DamperCalibration,
    MeatProbeMode,
    SmokerConfig,
)
from esp32_fan_control.Domain.control import (
    ControlCommand,
    ControlCommandKind,
    PersistenceStatus,
)
from esp32_fan_control.Domain.pid import PIDConfig
from esp32_fan_control.Domain.temperature import (
    SensorFault,
    SensorRole,
    TemperatureReading,
)
from esp32_fan_control.Services.smoker_control_service import SmokerControlService


class Sensor:
    def __init__(self, pit_f=190):
        self.pit = TemperatureReading.from_fahrenheit(pit_f, SensorRole.PIT, 0)
        self.food = TemperatureReading.from_fahrenheit(0, SensorRole.FOOD_1, 0, fault=SensorFault.DISCONNECTED)
        self.calls = 0

    def read_temperature(self, role):
        self.calls += 1
        return self.pit if role == SensorRole.PIT else self.food


class Actuators:
    def __init__(self):
        self.damper = self.blower = -1
        self.events = []
        self.calibration = None

    def configure(self, calibration):
        self.calibration = calibration
        self.events.append("configure")

    def set_position(self, value):
        assert value != 0 or self.blower <= 0
        self.damper = value
        self.events.append("close" if value == 0 else "open")

    def set_speed(self, value):
        assert value <= 0 or self.damper > 0
        self.blower = value
        self.events.append("off" if value == 0 else "on")


class Storage:
    def __init__(self, config=None):
        self.config = config
        self.loads = self.saves = 0
        self.succeed = True

    def load_config(self):
        self.loads += 1
        return self.config

    def save_config(self, config):
        self.saves += 1
        if self.succeed:
            self.config = config
        return self.succeed


class Channel:
    def __init__(self):
        self.commands = deque()
        self.state = None
        self.received = 0
        self.replenish = False

    def submit(self, command):
        if len(self.commands) >= 8:
            return False
        self.commands.append(command)
        return True

    def receive(self):
        if not self.commands:
            return None
        value = self.commands.popleft()
        self.received += 1
        if self.replenish:
            self.commands.append(value)
        return value

    def publish(self, state):
        self.state = state

    def snapshot(self):
        return self.state


def setup_control(pit_f=190, **kwargs):
    sensor = Sensor(pit_f)
    actuators = Actuators()
    service = SmokerControlService(sensor, actuators, actuators, **kwargs)
    return sensor, actuators, service


def test_storage_loading_occurs_only_in_explicit_idempotent_initialization():
    config = SmokerConfig(setpoint_f=265, servo_min_pulse_us=800, servo_max_pulse_us=2200,
                          servo_inverted=True)
    storage = Storage(config)
    sensor, actuators, service = setup_control(config_storage=storage)
    assert storage.loads == 0 and actuators.events == []
    assert service.execute_cycle(0).demand_pct == 0 and sensor.calls == 0
    actuators.events.clear()
    service.initialize()
    assert storage.loads == 1 and storage.saves == 0
    assert service.config == config and actuators.calibration == config.damper_calibration
    assert actuators.events == ["off", "configure", "close"]
    service.initialize()
    assert storage.loads == 1 and len(actuators.events) == 3


@pytest.mark.parametrize("fault", [SensorFault.DISCONNECTED, SensorFault.SHORT_TO_GND,
                                  SensorFault.SHORT_TO_VCC, SensorFault.OUT_OF_RANGE, SensorFault.STALE])
def test_active_outputs_shutdown_for_every_pit_fault(fault):
    sensor, actuators, service = setup_control()
    service.initialize()
    healthy = service.execute_cycle(0)
    assert healthy.meat_temp_f is None and healthy.pit_temp_f == 190
    assert actuators.blower > 0 and actuators.damper > 0
    sensor.pit = replace(sensor.pit, fault=fault)
    failed = service.execute_cycle(1)
    assert failed.pit_temp_f is None and failed.demand_pct == 0
    assert actuators.blower == actuators.damper == 0


@pytest.mark.parametrize("celsius", [-40.25, 500.25, math.nan, math.inf, -math.inf])
def test_out_of_range_or_nonfinite_pit_closes_active_outputs(celsius):
    sensor, actuators, service = setup_control()
    service.initialize()
    service.execute_cycle(0)
    assert actuators.blower > 0 and actuators.damper > 0
    sensor.pit = replace(sensor.pit, celsius=celsius)
    assert not sensor.pit.is_valid
    service.execute_cycle(1)
    assert actuators.blower == actuators.damper == 0


@pytest.mark.parametrize("pause", ["manual", "automatic", "fault"])
def test_pid_recovery_excludes_entire_suppression_interval(pause):
    sensor, _actuators, service = setup_control(pit_f=220, pid_config=PIDConfig(kp=0, ki=0.1, kd=0))
    service.initialize()
    service.execute_cycle(0)
    before = service.execute_cycle(1)
    assert before.demand_pct == 0.5
    if pause == "manual":
        service.trigger_lid_pause(2)
    elif pause == "automatic":
        sensor.pit = TemperatureReading.from_fahrenheit(190, SensorRole.PIT, 2)
    else:
        sensor.pit = replace(sensor.pit, fault=SensorFault.DISCONNECTED)
    assert service.execute_cycle(2).demand_pct == 0
    sensor.pit = TemperatureReading.from_fahrenheit(220, SensorRole.PIT, 182)
    resumed = service.execute_cycle(182)
    assert not resumed.lid_open and resumed.demand_pct == before.demand_pct


def test_setpoint_keeps_entire_configuration_and_does_not_cancel_pause():
    config = SmokerConfig(meat_probe_mode=MeatProbeMode.MEATER_CLOUD, meater_cloud_token="placeholder",
                          meater_mac_filter="11:22:33:44:55:66", servo_min_pulse_us=700,
                          servo_max_pulse_us=2300, servo_inverted=True)
    storage = Storage(config)
    _, _actuators, service = setup_control(config_storage=storage)
    service.initialize()
    service.trigger_lid_pause(0)
    service.setpoint_f = 250
    assert service.config == replace(config, setpoint_f=250)
    assert storage.config == service.config
    assert service.execute_cycle(1).lid_open
    _, restarted_actuators, restarted = setup_control(config_storage=storage)
    restarted.initialize()
    assert restarted.config == service.config
    assert restarted_actuators.calibration == config.damper_calibration


def test_calibration_update_and_persistence_failure_are_visible():
    storage = Storage()
    channel = Channel()
    _, actuators, service = setup_control(config_storage=storage, control_channel=channel)
    service.initialize()
    service.execute_cycle(0)
    assert actuators.blower > 0
    actuators.events.clear()
    updated = replace(service.config, servo_min_pulse_us=600, servo_max_pulse_us=2400, servo_inverted=True)
    assert service.update_config(updated)
    assert actuators.events == ["off", "configure", "close"]
    service.execute_cycle(1)
    assert channel.state.persistence == PersistenceStatus.SAVED
    storage.succeed = False
    service.setpoint_f = 280
    service.execute_cycle(2)
    assert channel.state.persistence == PersistenceStatus.FAILED
    assert channel.state.config.setpoint_f == 280 and storage.config.setpoint_f == 225


def test_command_handoff_validation_version_guard_and_bounded_work():
    channel = Channel()
    sensor, _actuators, service = setup_control(control_channel=channel)
    service.initialize()
    command = ControlCommand(ControlCommandKind.SET_SETPOINT, setpoint_f=250,
                             request_id=10, expected_config_version=0)
    assert channel.submit(command) and service.setpoint_f == 225
    assert service.execute_cycle(0).setpoint_f == 250
    state = channel.snapshot()
    assert state.last_command_accepted and state.config_version == 1
    channel.submit(replace(command, setpoint_f=275, request_id=11))
    service.execute_cycle(1)
    assert not channel.state.last_command_accepted and service.setpoint_f == 250
    assert channel.state.last_command_id == 11
    channel.submit(replace(command, setpoint_f=500, expected_config_version=1))
    service.execute_cycle(2)
    assert not channel.state.last_command_accepted and channel.state.config_version == 1
    assert state.config.setpoint_f == 250  # old immutable snapshot remains unchanged
    channel.submit(ControlCommand(ControlCommandKind.TRIGGER_LID_PAUSE))
    assert service.execute_cycle(3).lid_open
    channel.submit(ControlCommand(ControlCommandKind.CANCEL_LID_PAUSE))
    assert not service.execute_cycle(4).lid_open
    channel.submit(ControlCommand(ControlCommandKind.SET_SETPOINT, setpoint_f=255))
    channel.replenish = True
    received = channel.received
    service.execute_cycle(5)
    assert channel.received - received == 8 and sensor.calls == 12


@pytest.mark.parametrize("low, high, inverted", [(499, 2000, False), (1000, 2501, False),
    (2000, 2000, False), (2000, 1000, False), (1000.5, 2000, False), (True, 2000, False),
    (1000, 2000, "false")])
def test_invalid_calibration_is_rejected(low, high, inverted):
    with pytest.raises(ValueError):
        DamperCalibration(low, high, inverted)
    with pytest.raises(ValueError):
        SmokerConfig(servo_min_pulse_us=low, servo_max_pulse_us=high, servo_inverted=inverted)


@pytest.mark.parametrize("field", ["pid_kp", "pid_ki", "pid_kd"])
def test_nan_pid_config_is_rejected(field):
    with pytest.raises(ValueError):
        SmokerConfig(**{field: math.nan})
