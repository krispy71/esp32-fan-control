"""Temperature sensor port dependency interface."""

from __future__ import annotations

from typing import Protocol
from esp32_fan_control.Domain.temperature import SensorRole, TemperatureReading


class TemperatureSensorPort(Protocol):
    """Port for acquiring validated temperature readings from physical or wireless probes."""

    def read_temperature(self, role: SensorRole) -> TemperatureReading:
        """Read and return current temperature reading for the specified sensor role."""
        ...
