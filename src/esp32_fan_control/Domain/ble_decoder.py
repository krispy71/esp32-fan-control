"""
BLE thermometer advertisement packet decoders.
Supports open BTHome, Inkbird/ThermoPro, MEATER, and standard SIG Environmental Sensing formats.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from enum import Enum


class BLEProbeProtocol(Enum):
    BTHOME = "BTHome"
    INKBIRD = "Inkbird"
    MEATER = "MEATER"
    SIG_ENV = "SIG_Environmental"
    UNKNOWN = "Unknown"


@dataclass(frozen=True)
class BLEProbeReading:
    """Decoded temperature reading from a wireless Bluetooth probe."""
    internal_temp_c: float
    mac_address: str = ""
    probe_name: str = "Wireless Probe"
    ambient_temp_c: float | None = None
    battery_pct: int | None = None
    protocol: BLEProbeProtocol = BLEProbeProtocol.UNKNOWN

    @property
    def internal_temp_f(self) -> float:
        return (self.internal_temp_c * 9.0 / 5.0) + 32.0

    @property
    def ambient_temp_f(self) -> float | None:
        if self.ambient_temp_c is None:
            return None
        return (self.ambient_temp_c * 9.0 / 5.0) + 32.0


class BLEAdvertisementDecoder:
    """Pure domain decoder for wireless thermometer advertisement payloads."""

    @staticmethod
    def decode_bthome_v2(payload: bytes, mac: str = "") -> BLEProbeReading | None:
        """
        Decodes BTHome V2 service data payload (UUID 0xFCD2).
        Format: [Adv Header: 1B] [Object ID: 1B] [Data: variable length]...
        """
        if len(payload) < 4:
            return None

        idx = 1  # Skip BTHome flag header
        internal_temp_c: float | None = None
        battery_pct: int | None = None

        while idx < len(payload):
            obj_id = payload[idx]
            idx += 1

            if obj_id == 0x02:  # Temperature, signed 16-bit (factor 0.01)
                if idx + 2 > len(payload):
                    break
                raw_temp = struct.unpack("<h", payload[idx:idx + 2])[0]
                internal_temp_c = raw_temp * 0.01
                idx += 2
            elif obj_id == 0x01:  # Battery, uint8 (0-100%)
                if idx + 1 > len(payload):
                    break
                battery_pct = payload[idx]
                idx += 1
            else:
                # Unknown object, advance or break if at end
                break

        if internal_temp_c is not None:
            return BLEProbeReading(
                internal_temp_c=internal_temp_c,
                mac_address=mac,
                probe_name="BTHome Probe",
                battery_pct=battery_pct,
                protocol=BLEProbeProtocol.BTHOME,
            )
        return None

    @staticmethod
    def decode_inkbird(payload: bytes, mac: str = "") -> BLEProbeReading | None:
        """
        Decodes Inkbird / ThermoPro broadcast manufacturer data.
        Format: [Manufacturer ID: 2B] [Probe 1 Temp: 2B signed little-endian (0.1°C)] [Battery: 1B]...
        """
        if len(payload) < 5:
            return None

        try:
            # First 2 bytes are manufacturer ID or preamble; next 2 bytes are temp in 0.1°C
            raw_temp = struct.unpack("<h", payload[2:4])[0]
            temp_c = raw_temp * 0.1

            # Sanity check temperature range (-40°C to 350°C)
            if not (-40.0 <= temp_c <= 350.0):
                return None

            battery = payload[4] if len(payload) >= 5 and payload[4] <= 100 else None
            return BLEProbeReading(
                internal_temp_c=temp_c,
                mac_address=mac,
                probe_name="Inkbird/ThermoPro Probe",
                battery_pct=battery,
                protocol=BLEProbeProtocol.INKBIRD,
            )
        except Exception:
            return None

    @staticmethod
    def decode_meater(payload: bytes, mac: str = "") -> BLEProbeReading | None:
        """
        Decodes MEATER smart meat thermometer advertising / beacon payload.
        Contains tip (internal) and handle (ambient) temperatures in raw 16-bit format.
        """
        if len(payload) < 7:
            return None

        try:
            # MEATER format payload provides Tip and Ambient in consecutive 16-bit values
            raw_tip = struct.unpack(">h", payload[2:4])[0]
            raw_ambient = struct.unpack(">h", payload[4:6])[0]

            tip_c = (raw_tip * 0.1) if raw_tip > 0 else 0.0
            ambient_c = (raw_ambient * 0.1) if raw_ambient > 0 else 0.0

            if not (0.0 <= tip_c <= 120.0):
                return None

            battery = payload[6] if len(payload) >= 7 and payload[6] <= 100 else None
            return BLEProbeReading(
                internal_temp_c=tip_c,
                ambient_temp_c=ambient_c,
                mac_address=mac,
                probe_name="MEATER Probe",
                battery_pct=battery,
                protocol=BLEProbeProtocol.MEATER,
            )
        except Exception:
            return None

    @staticmethod
    def decode_sig_environmental(payload: bytes, mac: str = "") -> BLEProbeReading | None:
        """
        Decodes Bluetooth SIG Environmental Sensing (UUID 0x181A / 0x2A6E).
        Format: 16-bit signed integer in 0.01°C increments.
        """
        if len(payload) < 2:
            return None

        try:
            raw_temp = struct.unpack("<h", payload[:2])[0]
            temp_c = raw_temp * 0.01
            if not (-40.0 <= temp_c <= 300.0):
                return None

            return BLEProbeReading(
                internal_temp_c=temp_c,
                mac_address=mac,
                probe_name="SIG Wireless Probe",
                protocol=BLEProbeProtocol.SIG_ENV,
            )
        except Exception:
            return None

    @classmethod
    def decode_any(cls, payload: bytes, mac: str = "") -> BLEProbeReading | None:
        """Attempts decoding using all supported probe formats in sequence."""
        # 1. Try BTHome V2
        r = cls.decode_bthome_v2(payload, mac)
        if r is not None:
            return r

        # 2. Try MEATER
        r = cls.decode_meater(payload, mac)
        if r is not None:
            return r

        # 3. Try Inkbird / ThermoPro
        r = cls.decode_inkbird(payload, mac)
        if r is not None:
            return r

        # 4. Try SIG Environmental
        return cls.decode_sig_environmental(payload, mac)
