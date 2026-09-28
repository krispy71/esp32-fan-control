"""
Composite sensor adapter multiplexing wired thermocouple and wireless BLE probes.
"""

from __future__ import annotations

from esp32_fan_control.Domain.configuration import MeatProbeMode
from esp32_fan_control.Domain.temperature import SensorRole, TemperatureReading
from esp32_fan_control.Services.Ports.sensor_port import TemperatureSensorPort


class CompositeSensorAdapter(TemperatureSensorPort):
    """
    Multiplexes a primary wired thermocouple sensor port and selectable wireless meat probe sources:
      1. Passive BLE Broadcast (Inkbird, BTHome, ThermoPro)
      2. Direct MEATER (Active BLE GATT)
      3. MEATER Cloud REST API (Wi-Fi sync)
      4. Wired Only (MAX31855 Food probe)
    """

    def __init__(
        self,
        wired_sensor: TemperatureSensorPort,
        passive_ble_sensor: TemperatureSensorPort | None = None,
        meater_direct_sensor: TemperatureSensorPort | None = None,
        meater_cloud_sensor: TemperatureSensorPort | None = None,
        mode: MeatProbeMode = MeatProbeMode.PASSIVE_BLE,
        wireless_sensor: TemperatureSensorPort | None = None,
    ) -> None:
        self._wired_sensor = wired_sensor
        self._passive_ble_sensor = wireless_sensor if wireless_sensor is not None else passive_ble_sensor
        self._meater_direct_sensor = meater_direct_sensor
        self._meater_cloud_sensor = meater_cloud_sensor
        self._mode = mode

    def set_mode(self, mode: MeatProbeMode) -> None:
        self._mode = mode

    @property
    def mode(self) -> MeatProbeMode:
        return self._mode

    def set_wireless_sensor(self, wireless_sensor: TemperatureSensorPort | None) -> None:
        self._passive_ble_sensor = wireless_sensor

    def set_passive_ble_sensor(self, sensor: TemperatureSensorPort | None) -> None:
        self._passive_ble_sensor = sensor

    def set_meater_direct_sensor(self, sensor: TemperatureSensorPort | None) -> None:
        self._meater_direct_sensor = sensor

    def set_meater_cloud_sensor(self, sensor: TemperatureSensorPort | None) -> None:
        self._meater_cloud_sensor = sensor

    @property
    def has_wireless_sensor(self) -> bool:
        return (
            self._passive_ble_sensor is not None
            or self._meater_direct_sensor is not None
            or self._meater_cloud_sensor is not None
        )

    def read_temperature(self, role: SensorRole) -> TemperatureReading:
        # Pit probe must always be wired
        if role == SensorRole.PIT:
            return self._wired_sensor.read_temperature(role)

        if role == SensorRole.FOOD_1:
            active_sensor: TemperatureSensorPort | None = None
            if self._mode == MeatProbeMode.PASSIVE_BLE:
                active_sensor = self._passive_ble_sensor
            elif self._mode == MeatProbeMode.MEATER_BLE_DIRECT:
                active_sensor = self._meater_direct_sensor
            elif self._mode == MeatProbeMode.MEATER_CLOUD:
                active_sensor = self._meater_cloud_sensor
            elif self._mode == MeatProbeMode.WIRED_ONLY:
                active_sensor = None

            if active_sensor is not None:
                reading = active_sensor.read_temperature(role)
                if reading.is_valid:
                    return reading

            # Fallback to wired sensor
            return self._wired_sensor.read_temperature(role)

        if role in (SensorRole.FOOD_2, SensorRole.AMBIENT):
            active_sensor = None
            if self._mode == MeatProbeMode.MEATER_BLE_DIRECT:
                active_sensor = self._meater_direct_sensor
            elif self._mode == MeatProbeMode.MEATER_CLOUD:
                active_sensor = self._meater_cloud_sensor
            elif self._mode == MeatProbeMode.PASSIVE_BLE:
                active_sensor = self._passive_ble_sensor

            if active_sensor is not None:
                reading = active_sensor.read_temperature(role)
                if reading.is_valid:
                    return reading

        return self._wired_sensor.read_temperature(role)
