"""Tests for BLEProbeAdapter implementing TemperatureSensorPort."""

import pytest
from esp32_fan_control.Adapters.ble_probe_adapter import BLEProbeAdapter
from esp32_fan_control.Domain.temperature import SensorFault, SensorRole


def test_ble_probe_initial_state():
    adapter = BLEProbeAdapter(staleness_timeout_s=30.0)
    assert not adapter.is_connected(now_s=100.0)

    reading = adapter.read_temperature(SensorRole.FOOD_1)
    assert not reading.is_valid
    assert reading.fault == SensorFault.DISCONNECTED
    assert reading.is_wireless


def test_ble_probe_advertisement_processing():
    adapter = BLEProbeAdapter(staleness_timeout_s=30.0)
    adapter.set_mock_time(100.0)

    # MEATER payload: tip 55.0°C (131.0°F), ambient 110.0°C (230.0°F), battery 88%
    meater_payload = bytes([0x00, 0x00, 0x02, 0x26, 0x04, 0x4C, 0x58])
    processed = adapter.process_advertisement(meater_payload, mac="AA:BB:CC:DD:EE:FF", now_s=100.0)
    assert processed
    assert adapter.is_connected(now_s=105.0)

    # Read internal food temp
    r_food1 = adapter.read_temperature(SensorRole.FOOD_1)
    assert r_food1.is_valid
    assert pytest.approx(r_food1.celsius, rel=1e-3) == 55.0
    assert pytest.approx(r_food1.fahrenheit, rel=1e-3) == 131.0
    assert r_food1.is_wireless
    assert r_food1.battery_pct == 88
    assert r_food1.probe_name == "MEATER Probe"

    # Read ambient handle temp
    r_ambient = adapter.read_temperature(SensorRole.AMBIENT)
    assert r_ambient.is_valid
    assert pytest.approx(r_ambient.celsius, rel=1e-3) == 110.0
    assert pytest.approx(r_ambient.fahrenheit, rel=1e-3) == 230.0


def test_ble_probe_staleness_timeout():
    adapter = BLEProbeAdapter(staleness_timeout_s=30.0)
    # Receive packet at t=100s
    meater_payload = bytes([0x00, 0x00, 0x02, 0x26, 0x04, 0x4C, 0x58])
    adapter.process_advertisement(meater_payload, mac="AA:BB:CC:DD:EE:FF", now_s=100.0)

    # Query at t=120s (20s elapsed <= 30s) -> Connected
    assert adapter.is_connected(now_s=120.0)

    # Query at t=135s (35s elapsed > 30s) -> Stale
    assert not adapter.is_connected(now_s=135.0)
    adapter.set_mock_time(135.0)
    r_stale = adapter.read_temperature(SensorRole.FOOD_1)
    assert not r_stale.is_valid
    assert r_stale.fault == SensorFault.STALE


def test_ble_probe_mac_filtering():
    adapter = BLEProbeAdapter(staleness_timeout_s=30.0)
    adapter.set_target_mac("11:22:33:44:55:66")

    meater_payload = bytes([0x00, 0x00, 0x02, 0x26, 0x04, 0x4C, 0x58])

    # Packet from wrong MAC is ignored
    assert not adapter.process_advertisement(meater_payload, mac="99:88:77:66:55:44", now_s=100.0)
    assert not adapter.is_connected(now_s=100.0)

    # Packet from matching MAC is processed
    assert adapter.process_advertisement(meater_payload, mac="11:22:33:44:55:66", now_s=101.0)
    assert adapter.is_connected(now_s=101.0)
