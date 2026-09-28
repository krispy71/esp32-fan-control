"""
MEATER probe sensor adapters for active BLE GATT client and MEATER Cloud REST API.
"""

from __future__ import annotations

import json
from esp32_fan_control.Domain.ble_decoder import BLEAdvertisementDecoder, BLEProbeReading
from esp32_fan_control.Domain.temperature import SensorFault, SensorRole, TemperatureReading
from esp32_fan_control.Services.Ports.sensor_port import TemperatureSensorPort


class MeaterBleClientAdapter(TemperatureSensorPort):
    """
    Adapter representing an active BLE GATT connection to a genuine MEATER probe.
    Decodes characteristic 7EDDA774-045E-4BBF-909B-45D1991A2876 payloads.
    """

    def __init__(self, staleness_timeout_s: float = 30.0) -> None:
        self.staleness_timeout_s = staleness_timeout_s
        self._last_reading: BLEProbeReading | None = None
        self._last_packet_time_s: float = 0.0
        self._target_mac: str = ""
        self._enabled: bool = False

    def set_target_mac(self, mac: str) -> None:
        self._target_mac = mac

    def set_enabled(self, enabled: bool) -> None:
        self._enabled = enabled

    @property
    def is_enabled(self) -> bool:
        return self._enabled

    def is_connected(self, now_s: float) -> bool:
        return (
            self._last_reading is not None
            and (now_s - self._last_packet_time_s) <= self.staleness_timeout_s
        )

    def process_gatt_payload(self, payload: bytes, now_s: float, mac: str = "") -> bool:
        reading = BLEAdvertisementDecoder.decode_meater_gatt(payload, mac=mac)
        if reading is not None:
            self._last_reading = reading
            self._last_packet_time_s = now_s
            return True
        return False

    def read_temperature(self, role: SensorRole, now_s: float = 0.0) -> TemperatureReading:
        effective_now = now_s if now_s > 0 else self._last_packet_time_s
        if not self.is_connected(effective_now):
            fault = SensorFault.STALE if self._last_reading is not None else SensorFault.DISCONNECTED
            probe_name = self._last_reading.probe_name if self._last_reading else "MEATER (Direct)"
            battery = self._last_reading.battery_pct if self._last_reading else None
            return TemperatureReading(
                celsius=0.0,
                role=role,
                timestamp_s=effective_now,
                fault=fault,
                is_wireless=True,
                battery_pct=battery,
                probe_name=probe_name,
            )

        assert self._last_reading is not None
        if role == SensorRole.FOOD_1:
            return TemperatureReading(
                celsius=self._last_reading.internal_temp_c,
                role=role,
                timestamp_s=self._last_packet_time_s,
                fault=SensorFault.OK,
                is_wireless=True,
                battery_pct=self._last_reading.battery_pct,
                probe_name=self._last_reading.probe_name,
            )
        elif role in (SensorRole.FOOD_2, SensorRole.AMBIENT):
            if self._last_reading.ambient_temp_c is not None:
                return TemperatureReading(
                    celsius=self._last_reading.ambient_temp_c,
                    role=role,
                    timestamp_s=self._last_packet_time_s,
                    fault=SensorFault.OK,
                    is_wireless=True,
                    battery_pct=self._last_reading.battery_pct,
                    probe_name=self._last_reading.probe_name,
                )

        return TemperatureReading(
            celsius=0.0,
            role=role,
            timestamp_s=effective_now,
            fault=SensorFault.DISCONNECTED,
            is_wireless=True,
            probe_name=self._last_reading.probe_name,
        )


class MeaterCloudAdapter(TemperatureSensorPort):
    """
    Adapter that queries the MEATER Cloud public REST API (https://public-api.cloud.meater.com/v1/devices).
    """

    def __init__(self, staleness_timeout_s: float = 60.0) -> None:
        self.staleness_timeout_s = staleness_timeout_s
        self._api_token: str = ""
        self._last_reading: BLEProbeReading | None = None
        self._last_fetch_time_s: float = 0.0
        self._enabled: bool = False

    def set_api_token(self, token: str) -> None:
        self._api_token = token

    @property
    def api_token(self) -> str:
        return self._api_token

    def set_enabled(self, enabled: bool) -> None:
        self._enabled = enabled

    @property
    def is_enabled(self) -> bool:
        return self._enabled

    def is_connected(self, now_s: float) -> bool:
        return (
            self._last_reading is not None
            and (now_s - self._last_fetch_time_s) <= self.staleness_timeout_s
        )

    def process_cloud_payload(self, data: dict, now_s: float) -> bool:
        devices = data.get("data", {}).get("devices", [])
        if not devices and "devices" in data:
            devices = data["devices"]
        if not devices and "temperature" in data:
            devices = [data]

        if not devices:
            return False

        first_device = devices[0]
        reading = BLEAdvertisementDecoder.decode_meater_cloud_dict(first_device)
        if reading is not None:
            self._last_reading = reading
            self._last_fetch_time_s = now_s
            return True
        return False

    def read_temperature(self, role: SensorRole, now_s: float = 0.0) -> TemperatureReading:
        effective_now = now_s if now_s > 0 else self._last_fetch_time_s
        if not self.is_connected(effective_now):
            fault = SensorFault.STALE if self._last_reading is not None else SensorFault.DISCONNECTED
            probe_name = self._last_reading.probe_name if self._last_reading else "MEATER (Cloud)"
            battery = self._last_reading.battery_pct if self._last_reading else None
            return TemperatureReading(
                celsius=0.0,
                role=role,
                timestamp_s=effective_now,
                fault=fault,
                is_wireless=True,
                battery_pct=battery,
                probe_name=probe_name,
            )

        assert self._last_reading is not None
        if role == SensorRole.FOOD_1:
            return TemperatureReading(
                celsius=self._last_reading.internal_temp_c,
                role=role,
                timestamp_s=self._last_fetch_time_s,
                fault=SensorFault.OK,
                is_wireless=True,
                battery_pct=self._last_reading.battery_pct,
                probe_name=self._last_reading.probe_name,
            )
        elif role in (SensorRole.FOOD_2, SensorRole.AMBIENT):
            if self._last_reading.ambient_temp_c is not None:
                return TemperatureReading(
                    celsius=self._last_reading.ambient_temp_c,
                    role=role,
                    timestamp_s=self._last_fetch_time_s,
                    fault=SensorFault.OK,
                    is_wireless=True,
                    battery_pct=self._last_reading.battery_pct,
                    probe_name=self._last_reading.probe_name,
                )

        return TemperatureReading(
            celsius=0.0,
            role=role,
            timestamp_s=effective_now,
            fault=SensorFault.DISCONNECTED,
            is_wireless=True,
            probe_name=self._last_reading.probe_name,
        )
