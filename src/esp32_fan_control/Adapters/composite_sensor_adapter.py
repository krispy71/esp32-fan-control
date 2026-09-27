"""
Composite sensor adapter multiplexing wired thermocouple and wireless BLE probes.
"""

from __future__ import annotations

from esp32_fan_control.Domain.temperature import SensorRole, TemperatureReading
from esp32_fan_control.Services.Ports.sensor_port import TemperatureSensorPort


class CompositeSensorAdapter(TemperatureSensorPort):
    """
    Multiplexes a primary wired thermocouple sensor port and an optional wireless BLE probe.
    Pit temperature is strictly routed to the wired thermocouple to handle extreme chamber heat.
    Food temperature prioritizes the wireless probe when connected/valid, seamlessly falling back
    to the secondary wired food probe if the wireless probe disconnects or times out.
    """

    def __init__(
        self,
        wired_sensor: TemperatureSensorPort,
        wireless_sensor: TemperatureSensorPort | None = None,
    ) -> None:
        self._wired_sensor = wired_sensor
        self._wireless_sensor = wireless_sensor

    def set_wireless_sensor(self, wireless_sensor: TemperatureSensorPort | None) -> None:
        self._wireless_sensor = wireless_sensor

    @property
    def has_wireless_sensor(self) -> bool:
        return self._wireless_sensor is not None

    def read_temperature(self, role: SensorRole) -> TemperatureReading:
        # Pit probe must always be wired
        if role == SensorRole.PIT:
            return self._wired_sensor.read_temperature(role)

        # Food and ambient roles prefer wireless if active and healthy
        if self._wireless_sensor is not None:
            reading = self._wireless_sensor.read_temperature(role)
            if reading.is_valid:
                return reading

        # Fallback to wired sensor
        return self._wired_sensor.read_temperature(role)
