"""Domain configuration model and validation invariants."""

from __future__ import annotations

from dataclasses import dataclass


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

    def __post_init__(self) -> None:
        if not (100.0 <= self.setpoint_f <= 450.0):
            raise ValueError(f"setpoint_f {self.setpoint_f} must be between 100.0 and 450.0 °F")
        if self.pid_kp < 0.0 or self.pid_kp > 100.0:
            raise ValueError(f"pid_kp {self.pid_kp} must be between 0.0 and 100.0")
        if self.pid_ki < 0.0 or self.pid_ki > 10.0:
            raise ValueError(f"pid_ki {self.pid_ki} must be between 0.0 and 10.0")
        if self.pid_kd < 0.0 or self.pid_kd > 500.0:
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
