"""Smoker control orchestration service."""

from __future__ import annotations

from esp32_fan_control.Domain.airflow import ActuatorCoordinator, AirflowDemand
from esp32_fan_control.Domain.configuration import SmokerConfig
from esp32_fan_control.Domain.lid_detector import LidDetectorConfig, LidOpenDetector
from esp32_fan_control.Domain.pid import PIDConfig, PIDRegulator
from esp32_fan_control.Domain.temperature import SensorFault, SensorRole, TemperatureReading
from esp32_fan_control.Services.Ports.actuator_ports import BlowerActuatorPort, DamperActuatorPort
from esp32_fan_control.Services.Ports.config_storage_port import ConfigStoragePort
from esp32_fan_control.Services.Ports.sensor_port import TemperatureSensorPort
from esp32_fan_control.Services.Ports.telemetry_port import TelemetryPublisherPort, TelemetrySnapshot


class SmokerControlService:
    """
    Orchestrates the periodic smoker control loop:
    1. Read chamber (pit) and food temperatures.
    2. Check sensor health — on disconnect or fault, immediately clamp actuators to 0.
    3. Evaluate lid-open condition.
    4. Compute PID airflow demand.
    5. Translate demand via ActuatorCoordinator into coordinated damper and blower commands.
    6. Command hardware via ports.
    7. Emit telemetry.
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
    ) -> None:
        self._sensor = sensor_port
        self._damper = damper_port
        self._blower = blower_port
        self._telemetry = telemetry_port
        self._config_storage = config_storage

        # Load persisted config if available
        loaded_config: SmokerConfig | None = None
        if config is not None:
            loaded_config = config
        elif self._config_storage is not None:
            loaded_config = self._config_storage.load_config()

        if loaded_config is not None:
            self._config = loaded_config
        else:
            kp = pid_config.kp if pid_config else 3.0
            ki = pid_config.ki if pid_config else 0.02
            kd = pid_config.kd if pid_config else 15.0
            thresh = coordinator.blower_threshold_pct if coordinator else 40.0
            self._config = SmokerConfig(
                setpoint_f=target_setpoint_f,
                pid_kp=kp,
                pid_ki=ki,
                pid_kd=kd,
                airflow_threshold_pct=thresh,
            )

        self._setpoint_f = self._config.setpoint_f
        self._coordinator = coordinator or ActuatorCoordinator(
            blower_threshold_pct=self._config.airflow_threshold_pct
        )
        self._pid = PIDRegulator(
            target_setpoint=self._setpoint_f,
            config=PIDConfig(
                kp=self._config.pid_kp,
                ki=self._config.pid_ki,
                kd=self._config.pid_kd,
            ),
        )
        self._lid_detector = LidOpenDetector(
            LidDetectorConfig(
                drop_threshold_deg=self._config.lid_drop_threshold_deg,
                pause_duration_s=self._config.lid_pause_duration_s,
            )
        )

        self._last_pit_temp_f: float | None = None
        self._last_meat_temp_f: float | None = None
        self._is_fail_safe: bool = False
        self._status_message: str = "INITIALIZED"
        self._last_snapshot: TelemetrySnapshot | None = None

    @property
    def config(self) -> SmokerConfig:
        return self._config

    def update_config(self, new_config: SmokerConfig) -> None:
        """Update runtime configuration and persist to storage if configured."""
        self._config = new_config
        self._setpoint_f = new_config.setpoint_f
        self._pid.setpoint = new_config.setpoint_f
        self._pid.config = PIDConfig(
            kp=new_config.pid_kp,
            ki=new_config.pid_ki,
            kd=new_config.pid_kd,
        )
        self._coordinator = ActuatorCoordinator(blower_threshold_pct=new_config.airflow_threshold_pct)
        self._lid_detector = LidOpenDetector(
            LidDetectorConfig(
                drop_threshold_deg=new_config.lid_drop_threshold_deg,
                pause_duration_s=new_config.lid_pause_duration_s,
            )
        )
        if self._config_storage is not None:
            self._config_storage.save_config(new_config)

    @property
    def setpoint_f(self) -> float:
        return self._setpoint_f

    @setpoint_f.setter
    def setpoint_f(self, value: float) -> None:
        new_config = SmokerConfig(
            setpoint_f=value,
            pid_kp=self._config.pid_kp,
            pid_ki=self._config.pid_ki,
            pid_kd=self._config.pid_kd,
            airflow_threshold_pct=self._config.airflow_threshold_pct,
            lid_drop_threshold_deg=self._config.lid_drop_threshold_deg,
            lid_pause_duration_s=self._config.lid_pause_duration_s,
        )
        self.update_config(new_config)

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
        """Manually trigger lid-open airflow pause."""
        self._lid_detector.trigger(current_time_s)

    def cancel_lid_pause(self) -> None:
        """Manually cancel lid-open airflow pause and resume normal regulation."""
        self._lid_detector.reset()

    def execute_cycle(self, current_time_s: float) -> TelemetrySnapshot:
        """Execute one complete sampling and regulation iteration."""
        # 1. Read Pit Temperature
        pit_reading = self._sensor.read_temperature(SensorRole.PIT)
        meat_reading = self._sensor.read_temperature(SensorRole.FOOD_1)

        self._last_meat_temp_f = meat_reading.fahrenheit if meat_reading.is_valid else None

        # 2. Probe Fault / Fail-Safe Check
        if not pit_reading.is_valid:
            self._is_fail_safe = True
            self._status_message = f"FAULT: {pit_reading.fault.value.upper()}"
            self._damper.set_position(0.0)
            self._blower.set_speed(0.0)
            snapshot = TelemetrySnapshot(
                timestamp_s=current_time_s,
                pit_temp_f=None,
                meat_temp_f=self._last_meat_temp_f,
                setpoint_f=self._setpoint_f,
                damper_position_pct=0.0,
                blower_speed_pct=0.0,
                demand_pct=0.0,
                lid_open=False,
                status=self._status_message,
            )
            self._last_snapshot = snapshot
            if self._telemetry:
                self._telemetry.publish(snapshot)
            return snapshot

        self._is_fail_safe = False
        pit_temp_f = pit_reading.fahrenheit
        self._last_pit_temp_f = pit_temp_f

        # 3. Lid-Open Detection
        lid_open = self._lid_detector.update(pit_temp_f, current_time_s)
        if lid_open:
            self._status_message = "LID_OPEN"
            demand = AirflowDemand(0.0)
            targets = self._coordinator.coordinate(demand)
            self._damper.set_position(targets.damper_position_pct)
            self._blower.set_speed(targets.blower_speed_pct)
            snapshot = TelemetrySnapshot(
                timestamp_s=current_time_s,
                pit_temp_f=pit_temp_f,
                meat_temp_f=self._last_meat_temp_f,
                setpoint_f=self._setpoint_f,
                damper_position_pct=targets.damper_position_pct,
                blower_speed_pct=targets.blower_speed_pct,
                demand_pct=0.0,
                lid_open=True,
                status=self._status_message,
            )
            self._last_snapshot = snapshot
            if self._telemetry:
                self._telemetry.publish(snapshot)
            return snapshot

        # 4. Normal Closed-Loop Regulation
        demand = self._pid.compute(pit_temp_f, current_time_s)
        targets = self._coordinator.coordinate(demand)

        # 5. Actuate Ports
        self._damper.set_position(targets.damper_position_pct)
        self._blower.set_speed(targets.blower_speed_pct)

        self._status_message = "REGULATING"
        snapshot = TelemetrySnapshot(
            timestamp_s=current_time_s,
            pit_temp_f=round(pit_temp_f, 1),
            meat_temp_f=round(self._last_meat_temp_f, 1) if self._last_meat_temp_f is not None else None,
            setpoint_f=self._setpoint_f,
            damper_position_pct=targets.damper_position_pct,
            blower_speed_pct=targets.blower_speed_pct,
            demand_pct=demand.value_pct,
            lid_open=False,
            status=self._status_message,
        )
        self._last_snapshot = snapshot

        if self._telemetry:
            self._telemetry.publish(snapshot)

        return snapshot
