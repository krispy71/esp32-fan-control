"""Immutable telemetry values owned by the domain."""

from dataclasses import dataclass


@dataclass(frozen=True)
class TelemetrySnapshot:
    """Instantaneous snapshot of controller state for reporting/logging."""
    timestamp_s: float
    pit_temp_f: float | None
    meat_temp_f: float | None
    setpoint_f: float
    damper_position_pct: float
    blower_speed_pct: float
    demand_pct: float
    lid_open: bool
    status: str
    is_meat_wireless: bool = False
    meat_battery_pct: int | None = None
    meat_probe_name: str = ""

