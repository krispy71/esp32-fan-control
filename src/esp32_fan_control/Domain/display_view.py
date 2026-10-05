"""Domain model for display formatting and refresh change detection."""

from __future__ import annotations

import math
from dataclasses import dataclass

from esp32_fan_control.Domain.telemetry import TelemetrySnapshot


@dataclass(frozen=True)
class DisplayView:
    """Pure domain representation of the smoker status formatted for visual display.
    
    Contains formatted views and significance checks to minimize e-ink flicker and wear.
    """
    pit_temp_f: float | None = None
    meat_temp_f: float | None = None
    setpoint_f: float = 225.0
    damper_pct: float = 0.0
    blower_pct: float = 0.0
    demand_pct: float = 0.0
    lid_open: bool = False
    status: str = "INITIALIZING"
    is_meat_wireless: bool = False
    meat_battery_pct: int | None = None
    meat_probe_name: str = ""
    timestamp_s: float = 0.0

    @property
    def pit_valid(self) -> bool:
        """Whether pit sensor reading is valid and finite."""
        return self.pit_temp_f is not None and math.isfinite(self.pit_temp_f)

    @property
    def meat_valid(self) -> bool:
        """Whether meat sensor reading is valid and finite."""
        return self.meat_temp_f is not None and math.isfinite(self.meat_temp_f)

    @classmethod
    def from_telemetry(cls, s: TelemetrySnapshot) -> DisplayView:
        """Construct a display value from the domain telemetry snapshot."""
        return cls(
            pit_temp_f=s.pit_temp_f,
            meat_temp_f=s.meat_temp_f,
            setpoint_f=float(s.setpoint_f),
            damper_pct=float(s.damper_position_pct),
            blower_pct=float(s.blower_speed_pct),
            demand_pct=s.demand_pct,
            lid_open=bool(s.lid_open),
            status=str(s.status),
            is_meat_wireless=s.is_meat_wireless,
            meat_battery_pct=s.meat_battery_pct,
            meat_probe_name=s.meat_probe_name,
            timestamp_s=s.timestamp_s,
        )

    def has_significant_change(
        self,
        prev: DisplayView | None,
        temp_thresh: float = 0.5,
        output_thresh: float = 5.0,
    ) -> bool:
        """Determines if telemetry has changed enough to warrant an e-ink refresh."""
        if prev is None:
            return True

        if (
            self.pit_valid != prev.pit_valid
            or self.meat_valid != prev.meat_valid
            or self.lid_open != prev.lid_open
            or self.status != prev.status
        ):
            return True

        if abs(self.setpoint_f - prev.setpoint_f) >= 0.5:
            return True

        if (self.pit_valid and prev.pit_valid
                and abs(self.pit_temp_f - prev.pit_temp_f) >= temp_thresh):  # type: ignore[operator]
            return True

        if (self.meat_valid and prev.meat_valid
                and abs(self.meat_temp_f - prev.meat_temp_f) >= temp_thresh):  # type: ignore[operator]
            return True

        return (
            abs(self.damper_pct - prev.damper_pct) >= output_thresh
            or abs(self.blower_pct - prev.blower_pct) >= output_thresh
        )

    def format_pit(self) -> str:
        """Format pit temperature string."""
        if not self.pit_valid:
            return "ERR"
        return f"{self.pit_temp_f:.1f} F"

    def format_meat(self) -> str:
        """Format meat temperature string."""
        if not self.meat_valid:
            return "--.- F"
        return f"{self.meat_temp_f:.1f} F"

    def format_setpoint(self) -> str:
        """Format target setpoint string."""
        return f"Set: {self.setpoint_f:.0f} F"
