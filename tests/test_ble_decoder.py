"""Tests for pure domain BLE thermometer advertisement decoders."""

import struct
import pytest
from esp32_fan_control.Domain.ble_decoder import (
    BLEAdvertisementDecoder,
    BLEProbeProtocol,
    BLEProbeReading,
)


def test_bthome_v2_decoder():
    # BTHome V2: Flag header 0x40, Obj 0x02 (temp 25.00°C = 2500 -> 0x09C4), Obj 0x01 (battery 85% -> 0x55)
    payload = bytes([0x40, 0x02, 0xC4, 0x09, 0x01, 0x55])
    reading = BLEAdvertisementDecoder.decode_bthome_v2(payload, mac="11:22:33:44:55:66")

    assert reading is not None
    assert isinstance(reading, BLEProbeReading)
    assert pytest.approx(reading.internal_temp_c, rel=1e-3) == 25.0
    assert pytest.approx(reading.internal_temp_f, rel=1e-3) == 77.0
    assert reading.battery_pct == 85
    assert reading.mac_address == "11:22:33:44:55:66"
    assert reading.protocol == BLEProbeProtocol.BTHOME
    assert reading.probe_name == "BTHome Probe"


def test_bthome_v2_invalid_short():
    assert BLEAdvertisementDecoder.decode_bthome_v2(b"\x40\x02") is None


def test_inkbird_decoder():
    # Inkbird: preamble [0x00, 0x00], temp 55.4°C (554 -> 0x022A little-endian), battery 92% (0x5C)
    payload = bytes([0x00, 0x00, 0x2A, 0x02, 0x5C])
    reading = BLEAdvertisementDecoder.decode_inkbird(payload, mac="AA:BB:CC:DD:EE:FF")

    assert reading is not None
    assert pytest.approx(reading.internal_temp_c, rel=1e-3) == 55.4
    assert reading.battery_pct == 92
    assert reading.protocol == BLEProbeProtocol.INKBIRD
    assert reading.probe_name == "Inkbird/ThermoPro Probe"


def test_inkbird_out_of_range():
    # Out of physical range temperature (> 350°C)
    payload = bytes([0x00, 0x00, 0xFF, 0x7F, 0x50])
    assert BLEAdvertisementDecoder.decode_inkbird(payload) is None


def test_meater_decoder():
    # MEATER: preamble [0x00, 0x00], tip 55.0°C (550 -> 0x0226 big-endian),
    # ambient 110.0°C (1100 -> 0x044C big-endian), battery 88% (0x58)
    payload = bytes([0x00, 0x00, 0x02, 0x26, 0x04, 0x4C, 0x58])
    reading = BLEAdvertisementDecoder.decode_meater(payload, mac="ME:AT:ER:01:02:03")

    assert reading is not None
    assert pytest.approx(reading.internal_temp_c, rel=1e-3) == 55.0
    assert reading.ambient_temp_c is not None
    assert pytest.approx(reading.ambient_temp_c, rel=1e-3) == 110.0
    assert pytest.approx(reading.internal_temp_f, rel=1e-3) == 131.0
    assert pytest.approx(reading.ambient_temp_f, rel=1e-3) == 230.0
    assert reading.battery_pct == 88
    assert reading.protocol == BLEProbeProtocol.MEATER


def test_meater_invalid():
    # Too short
    assert BLEAdvertisementDecoder.decode_meater(b"\x00\x00\x01") is None


def test_sig_environmental_decoder():
    # 21.50°C -> 2150 (0x0866 little-endian)
    payload = bytes([0x66, 0x08])
    reading = BLEAdvertisementDecoder.decode_sig_environmental(payload)

    assert reading is not None
    assert pytest.approx(reading.internal_temp_c, rel=1e-3) == 21.5
    assert reading.protocol == BLEProbeProtocol.SIG_ENV


def test_decode_any():
    # BTHome payload via decode_any
    bthome = bytes([0x40, 0x02, 0xC4, 0x09, 0x01, 0x55])
    r = BLEAdvertisementDecoder.decode_any(bthome)
    assert r is not None
    assert r.protocol == BLEProbeProtocol.BTHOME

    # MEATER payload via decode_any
    meater = bytes([0x00, 0x00, 0x02, 0x26, 0x04, 0x4C, 0x58])
    r_m = BLEAdvertisementDecoder.decode_any(meater)
    assert r_m is not None
    assert r_m.protocol == BLEProbeProtocol.MEATER
