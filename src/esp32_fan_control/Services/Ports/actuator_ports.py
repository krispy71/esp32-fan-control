"""Actuator port contracts for damper servo and blower fan."""

from __future__ import annotations

from typing import Protocol

from esp32_fan_control.Domain.configuration import DamperCalibration


class DamperActuatorPort(Protocol):
    """Port for controlling physical servo damper aperture."""

    def configure(self, calibration: DamperCalibration) -> None:
        """Apply validated pulse endpoints and direction."""
        ...

    def set_position(self, position_pct: float) -> None:
        """
        Command damper position (0.0% = fully closed, 100.0% = fully open).
        Must clamp inputs outside [0.0, 100.0].
        """
        ...


class BlowerActuatorPort(Protocol):
    """Port for controlling forced-air blower fan duty cycle."""

    def set_speed(self, speed_pct: float) -> None:
        """
        Command blower fan speed (0.0% = off, 100.0% = full RPM).
        Must clamp inputs outside [0.0, 100.0].
        """
        ...
