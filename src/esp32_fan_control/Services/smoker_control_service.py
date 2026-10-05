"""Smoker regulation owned by one control task, with copied command handoff."""

from __future__ import annotations

from dataclasses import replace
from esp32_fan_control.Domain.airflow import ActuatorCoordinator
from esp32_fan_control.Domain.configuration import SmokerConfig
from esp32_fan_control.Domain.control import ControlCommandKind, ControlState, PersistenceStatus
from esp32_fan_control.Domain.lid_detector import LidDetectorConfig, LidOpenDetector
from esp32_fan_control.Domain.pid import PIDConfig, PIDRegulator
from esp32_fan_control.Domain.temperature import SensorRole
from esp32_fan_control.Services.Ports.actuator_ports import BlowerActuatorPort, DamperActuatorPort
from esp32_fan_control.Services.Ports.config_storage_port import ConfigStoragePort
from esp32_fan_control.Services.Ports.control_channel_port import ControlChannelPort
from esp32_fan_control.Services.Ports.sensor_port import TemperatureSensorPort
from esp32_fan_control.Services.Ports.telemetry_port import TelemetryPublisherPort, TelemetrySnapshot


class SmokerControlService:
    """Read sensors, enforce inhibition, compute demand, actuate and publish state.

    All methods belong to the control task. Network callers use ControlChannelPort.
    Construction has no external effects; call initialize after platform startup.
    """

    def __init__(
        self,
        sensor_port: TemperatureSensorPort,
        damper_port: DamperActuatorPort,
        blower_port: BlowerActuatorPort,
        telemetry_port: TelemetryPublisherPort | None = None,
        target_setpoint_f: float = 225.0,
        coordinator: ActuatorCoordinator | None = None,
        pid_config: PIDConfig | None = None,
        config_storage: ConfigStoragePort | None = None,
        config: SmokerConfig | None = None,
        control_channel: ControlChannelPort | None = None,
    ) -> None:
        self._sensor = sensor_port
        self._damper = damper_port
        self._blower = blower_port
        self._telemetry = telemetry_port
        self._config_storage = config_storage
        self._control_channel = control_channel
        self._explicit_config = config is not None
        tuning = pid_config or PIDConfig()
        self._config = config or SmokerConfig(
            setpoint_f=target_setpoint_f,
            pid_kp=tuning.kp, pid_ki=tuning.ki, pid_kd=tuning.kd,
            airflow_threshold_pct=coordinator.blower_threshold_pct if coordinator else 40.0,
        )
        self._coordinator = coordinator or ActuatorCoordinator()
        self._pid = PIDRegulator(self._config.setpoint_f, tuning)
        self._lid_detector = LidOpenDetector()
        self._initialized = False
        self._is_fail_safe = False
        self._last_snapshot: TelemetrySnapshot | None = None
        self._persistence = PersistenceStatus.NOT_CONFIGURED
        self._last_command_id = 0
        self._last_command_accepted = False
        self._config_version = 0

    def initialize(self) -> None:
        """Load persisted settings, configure safe closure, and publish initial state."""
        if self._initialized:
            return
        self._blower.set_speed(0.0)
        if self._config_storage is not None:
            if not self._explicit_config:
                loaded = self._config_storage.load_config()
                if loaded is not None:
                    self._config = loaded
            self._persistence = PersistenceStatus.UNCHANGED
        self._apply_configuration()
        self._damper.configure(self._config.damper_calibration)
        self._damper.set_position(0.0)
        self._initialized = True
        self._publish_state()

    @property
    def config(self) -> SmokerConfig:
        return self._config

    def _apply_configuration(self) -> None:
        self._pid.setpoint = self._config.setpoint_f
        self._pid.config = replace(self._pid.config, kp=self._config.pid_kp,
                                   ki=self._config.pid_ki, kd=self._config.pid_kd)
        self._coordinator = ActuatorCoordinator(self._config.airflow_threshold_pct)
        self._lid_detector.configure(LidDetectorConfig(
            drop_threshold_deg=self._config.lid_drop_threshold_deg,
            pause_duration_s=self._config.lid_pause_duration_s,
        ))

    def update_config(self, new_config: SmokerConfig) -> bool:
        """Apply a complete validated config and report persistence separately."""
        if not self._initialized:
            return False
        old_calibration = self._config.damper_calibration
        self._config = new_config
        self._apply_configuration()
        if old_calibration != new_config.damper_calibration:
            self._blower.set_speed(0.0)
            self._damper.configure(new_config.damper_calibration)
            self._damper.set_position(0.0)
            self._pid.suspend()
        self._config_version += 1
        if self._config_storage is not None:
            self._persistence = (PersistenceStatus.SAVED
                if self._config_storage.save_config(new_config) else PersistenceStatus.FAILED)
        return True

    @property
    def setpoint_f(self) -> float:
        return self._config.setpoint_f

    @setpoint_f.setter
    def setpoint_f(self, value: float) -> None:
        self.update_config(replace(self._config, setpoint_f=value))

    @property
    def is_fail_safe(self) -> bool:
        return self._is_fail_safe

    @property
    def is_lid_open(self) -> bool:
        return self._lid_detector.is_active

    @property
    def last_snapshot(self) -> TelemetrySnapshot | None:
        return self._last_snapshot

    def trigger_lid_pause(self, current_time_s: float) -> None:
        self._lid_detector.trigger(current_time_s)
        self._pid.suspend()

    def cancel_lid_pause(self) -> None:
        self._lid_detector.reset()

    def _consume_commands(self, now: float) -> None:
        if self._control_channel is None:
            return
        # Constant upper bound even when a producer replenishes the queue.
        for _ in range(ControlChannelPort.capacity):
            command = self._control_channel.receive()
            if command is None:
                break
            self._last_command_id = command.request_id
            self._last_command_accepted = False
            if (command.kind in (ControlCommandKind.SET_SETPOINT, ControlCommandKind.UPDATE_CONFIG)
                    and command.expected_config_version is not None
                    and command.expected_config_version != self._config_version):
                continue
            try:
                if command.kind == ControlCommandKind.SET_SETPOINT:
                    self._last_command_accepted = self.update_config(
                        replace(self._config, setpoint_f=command.setpoint_f))
                elif command.kind == ControlCommandKind.UPDATE_CONFIG and command.config is not None:
                    self._last_command_accepted = self.update_config(command.config)
                elif command.kind == ControlCommandKind.TRIGGER_LID_PAUSE:
                    self.trigger_lid_pause(now)
                    self._last_command_accepted = True
                elif command.kind == ControlCommandKind.CANCEL_LID_PAUSE:
                    self.cancel_lid_pause()
                    self._last_command_accepted = True
            except (ValueError, TypeError):
                # Malformed command is rejected without interrupting regulation.
                self._last_command_accepted = False

    def _publish_state(self) -> None:
        if self._control_channel is not None:
            self._control_channel.publish(ControlState(
                self._config, self._last_snapshot, self._persistence,
                self._last_command_id, self._last_command_accepted, self._config_version,
            ))

    def _publish(self, snapshot: TelemetrySnapshot) -> TelemetrySnapshot:
        self._last_snapshot = snapshot
        self._publish_state()
        if self._telemetry is not None:
            self._telemetry.publish(snapshot)
        return snapshot

    def _stop_airflow(self) -> None:
        self._blower.set_speed(0.0)
        self._damper.set_position(0.0)

    def execute_cycle(self, current_time_s: float) -> TelemetrySnapshot:
        if not self._initialized:
            self._stop_airflow()
            return self._publish(TelemetrySnapshot(
                current_time_s, None, None, self.setpoint_f, 0.0, 0.0, 0.0, False, "INITIALIZING"))
        self._consume_commands(current_time_s)
        pit = self._sensor.read_temperature(SensorRole.PIT)
        meat = self._sensor.read_temperature(SensorRole.FOOD_1)
        pit_f = pit.fahrenheit if pit.is_valid else None
        meat_f = meat.fahrenheit if meat.is_valid else None
        self._is_fail_safe = not pit.is_valid
        lid_open = False
        demand_pct = damper_pct = blower_pct = 0.0
        if not pit.is_valid:
            self._pid.suspend()
            if not self._lid_detector.is_active:
                self._lid_detector.reset()
            self._stop_airflow()
            status = "FAULT: SENSOR_INVALID"
        else:
            lid_open = self._lid_detector.update(pit.fahrenheit, current_time_s)
            if lid_open:
                self._pid.suspend()
                self._stop_airflow()
                status = "LID_OPEN"
            else:
                demand = self._pid.compute(pit.fahrenheit, current_time_s)
                targets = self._coordinator.coordinate(demand)
                demand_pct = demand.value_pct
                damper_pct = targets.damper_position_pct
                blower_pct = targets.blower_speed_pct
                if blower_pct == 0.0:
                    self._blower.set_speed(0.0)
                self._damper.set_position(damper_pct)
                if blower_pct > 0.0:
                    self._blower.set_speed(blower_pct)
                status = "REGULATING"
        return self._publish(TelemetrySnapshot(
            current_time_s, pit_f, meat_f, self.setpoint_f,
            damper_pct, blower_pct, demand_pct, lid_open, status,
            meat.is_wireless, meat.battery_pct, meat.probe_name,
        ))
