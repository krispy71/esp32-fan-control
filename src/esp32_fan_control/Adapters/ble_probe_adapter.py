"""
BLE thermometer probe adapter.
Listens for or decodes BLE advertisement packets and implements TemperatureSensorPort.
"""

from __future__ import annotations

import time
from esp32_fan_control.Domain.ble_decoder import BLEAdvertisementDecoder, BLEProbeReading
from esp32_fan_control.Domain.temperature import SensorFault, SensorRole, TemperatureReading
from esp32_fan_control.Services.Ports.sensor_port import TemperatureSensorPort


class BLEProbeAdapter(TemperatureSensorPort):
    """
    Adapter implementing TemperatureSensorPort for Bluetooth Low Energy thermometers.
    Supports passive advertisement decoding (BTHome, Inkbird, MEATER, SIG) with staleness timeout.
    """

    def __init__(self, staleness_timeout_s: float = 30.0) -> None:
        self._staleness_timeout_s = staleness_timeout_s
        self._target_mac: str | None = None
        self._last_reading: BLEProbeReading | None = None
        self._last_packet_time_s: float = 0.0
        self._has_received_packet: bool = False
        self._mock_time_s: float | None = None

    def set_target_mac(self, mac: str | None) -> None:
        """Filter advertisements to a specific probe MAC address."""
        self._target_mac = mac.upper() if mac else None

    @property
    def target_mac(self) -> str | None:
        return self._target_mac

    @property
    def last_reading(self) -> BLEProbeReading | None:
        return self._last_reading

    def set_mock_time(self, now_s: float | None) -> None:
        """Override current time for deterministic testing."""
        self._mock_time_s = now_s

    def _get_now(self) -> float:
        if self._mock_time_s is not None:
            return self._mock_time_s
        return time.time()

    def is_connected(self, now_s: float | None = None) -> bool:
        """Check if probe packet was received within staleness window."""
        if not self._has_received_packet:
            return False
        current_time = now_s if now_s is not None else self._get_now()
        return (current_time - self._last_packet_time_s) <= self._staleness_timeout_s

    def process_advertisement(
        self,
        payload: bytes,
        mac: str = "",
        now_s: float | None = None,
    ) -> bool:
        """Process a raw BLE advertisement or service data payload."""
        if not payload:
            return False

        if self._target_mac and mac:
            if self._target_mac.upper() != mac.upper():
                return False

        reading = BLEAdvertisementDecoder.decode_any(payload, mac=mac)
        if reading is not None:
            if now_s is not None:
                self._mock_time_s = now_s
            self._last_reading = reading
            self._last_packet_time_s = now_s if now_s is not None else self._get_now()
            self._has_received_packet = True
            return True
        return False

    def simulate_reading(
        self,
        internal_temp_c: float,
        ambient_temp_c: float | None = None,
        battery_pct: int = 100,
        probe_name: str = "Simulated Wireless Probe",
        now_s: float | None = None,
    ) -> None:
        """Simulate an active probe packet for testing or desktop running."""
        if now_s is not None:
            self._mock_time_s = now_s
        current_time = now_s if now_s is not None else self._get_now()
        self._last_reading = BLEProbeReading(
            internal_temp_c=internal_temp_c,
            ambient_temp_c=ambient_temp_c,
            battery_pct=battery_pct,
            probe_name=probe_name,
            mac_address="AA:BB:CC:DD:EE:FF",
        )
        self._last_packet_time_s = current_time
        self._has_received_packet = True

    def read_temperature(self, role: SensorRole) -> TemperatureReading:
        now = self._get_now()

        if not self.is_connected(now):
            return TemperatureReading(
                celsius=0.0,
                role=role,
                timestamp_s=now,
                fault=SensorFault.STALE if self._has_received_packet else SensorFault.DISCONNECTED,
                is_wireless=True,
                battery_pct=self._last_reading.battery_pct if self._last_reading else None,
                probe_name=self._last_reading.probe_name if self._last_reading else "Wireless Probe",
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
            timestamp_s=now,
            fault=SensorFault.DISCONNECTED,
            is_wireless=True,
            battery_pct=None,
            probe_name="Wireless Probe",
        )
