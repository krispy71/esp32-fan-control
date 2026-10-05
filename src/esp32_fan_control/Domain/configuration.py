"""Domain configuration model and validation invariants."""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum


class MeatProbeMode(str, Enum):
    WIRED_ONLY = "wired"
    PASSIVE_BLE = "passive_ble"
    MEATER_BLE_DIRECT = "meater_direct"
    MEATER_CLOUD = "meater_cloud"


@dataclass(frozen=True)
class DamperCalibration:
    min_pulse_us: int = 1000
    max_pulse_us: int = 2000
    inverted: bool = False

    def __post_init__(self) -> None:
        if (type(self.min_pulse_us) is not int or type(self.max_pulse_us) is not int
                or not 500 <= self.min_pulse_us < self.max_pulse_us <= 2500
                or type(self.inverted) is not bool):
            raise ValueError("Calibration requires integer 500..2500 us endpoints, min < max, and boolean inversion")


@dataclass(frozen=True)
class SmokerConfig:
    """
    Configurable parameters for smoker regulation and safety.
    Enforces validation invariants to ensure operating boundaries remain safe.
    """
    setpoint_f: float = 225.0
    pid_kp: float = 3.0
    pid_ki: float = 0.02
    pid_kd: float = 15.0
    airflow_threshold_pct: float = 40.0
    lid_drop_threshold_deg: float = 15.0
    lid_pause_duration_s: float = 180.0
    meat_probe_mode: MeatProbeMode = MeatProbeMode.PASSIVE_BLE
    meater_cloud_token: str = ""
    meater_mac_filter: str = ""

    servo_min_pulse_us: int = 1000
    servo_max_pulse_us: int = 2000
    servo_inverted: bool = False

    @property
    def damper_calibration(self) -> DamperCalibration:
        return DamperCalibration(self.servo_min_pulse_us, self.servo_max_pulse_us, self.servo_inverted)

    def __post_init__(self) -> None:
        _ = self.damper_calibration  # Validate at the configuration boundary.
        if not isinstance(self.meat_probe_mode, MeatProbeMode):
            raise TypeError("Unknown meat probe mode")
        if not isinstance(self.meater_cloud_token, str) or len(self.meater_cloud_token) > 95:
            raise ValueError("Cloud token exceeds the device capacity")
        if not isinstance(self.meater_mac_filter, str) or len(self.meater_mac_filter) > 17:
            raise ValueError("MAC filter exceeds the device capacity")
        if not (100.0 <= self.setpoint_f <= 450.0):
            raise ValueError(f"setpoint_f {self.setpoint_f} must be between 100.0 and 450.0 °F")
        if not 0.0 <= self.pid_kp <= 100.0:
            raise ValueError(f"pid_kp {self.pid_kp} must be between 0.0 and 100.0")
        if not 0.0 <= self.pid_ki <= 10.0:
            raise ValueError(f"pid_ki {self.pid_ki} must be between 0.0 and 10.0")
        if not 0.0 <= self.pid_kd <= 500.0:
            raise ValueError(f"pid_kd {self.pid_kd} must be between 0.0 and 500.0")
        if not (10.0 <= self.airflow_threshold_pct <= 90.0):
            raise ValueError(
                f"airflow_threshold_pct {self.airflow_threshold_pct} must be between 10.0 and 90.0 %"
            )
        if not (5.0 <= self.lid_drop_threshold_deg <= 50.0):
            raise ValueError(
                f"lid_drop_threshold_deg {self.lid_drop_threshold_deg} must be between 5.0 and 50.0 °F"
            )
        if not (10.0 <= self.lid_pause_duration_s <= 600.0):
            raise ValueError(
                f"lid_pause_duration_s {self.lid_pause_duration_s} must be between 10.0 and 600.0 seconds"
            )
