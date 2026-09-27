"""Temperature reading domain entities and sensor fault models."""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum


class SensorRole(str, Enum):
    """Functional role of the temperature sensor."""
    PIT = "pit"
    FOOD_1 = "food_1"
    FOOD_2 = "food_2"
    AMBIENT = "ambient"


class SensorFault(str, Enum):
    """Hardware diagnostic states for thermocouple and probe sensors."""
    OK = "ok"
    DISCONNECTED = "disconnected"
    SHORT_TO_VCC = "short_to_vcc"
    SHORT_TO_GND = "short_to_gnd"
    OUT_OF_RANGE = "out_of_range"
    STALE = "stale"


@dataclass(frozen=True)
class TemperatureReading:
    """Validated temperature measurement at a given timestamp."""
    celsius: float
    role: SensorRole
    timestamp_s: float
    fault: SensorFault = SensorFault.OK

    @property
    def fahrenheit(self) -> float:
        """Convert Celsius measurement to Fahrenheit."""
        return (self.celsius * 9.0 / 5.0) + 32.0

    @classmethod
    def from_fahrenheit(
        cls,
        fahrenheit: float,
        role: SensorRole,
        timestamp_s: float,
        fault: SensorFault = SensorFault.OK,
    ) -> TemperatureReading:
        """Construct reading from Fahrenheit."""
        celsius = (fahrenheit - 32.0) * 5.0 / 9.0
        return cls(celsius=celsius, role=role, timestamp_s=timestamp_s, fault=fault)

    @property
    def is_valid(self) -> bool:
        """Verify whether reading is healthy and physically plausible."""
        if self.fault != SensorFault.OK:
            return False
        # Plausible temperature range: -40°C to 500°C (-40°F to 932°F)
        return -40.0 <= self.celsius <= 500.0
