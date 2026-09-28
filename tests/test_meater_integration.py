"""
Unit tests for MEATER GATT decoding, MEATER Cloud payload decoding, and multi-mode CompositeSensor routing.
"""

import pytest
from esp32_fan_control.Domain.ble_decoder import BLEAdvertisementDecoder, BLEProbeProtocol
from esp32_fan_control.Domain.configuration import MeatProbeMode, SmokerConfig
from esp32_fan_control.Domain.temperature import SensorFault, SensorRole, TemperatureReading
from esp32_fan_control.Adapters.composite_sensor_adapter import CompositeSensorAdapter
from esp32_fan_control.Adapters.meater_adapters import MeaterBleClientAdapter, MeaterCloudAdapter
from esp32_fan_control.Services.Ports.sensor_port import TemperatureSensorPort


class MockSensor(TemperatureSensorPort):
    def __init__(self, temp_f: float = 225.0) -> None:
        self.reading = TemperatureReading.from_fahrenheit(temp_f, SensorRole.PIT, 1000)

    def read_temperature(self, role: SensorRole) -> TemperatureReading:
        return self.reading


def test_decode_meater_gatt():
    # Tip raw = 872 (0x0368) -> (872 + 8) / 16 = 55.0°C (131.0°F)
    # ra = 200 (0x00C8), oa = 48 (0x0030)
    payload = bytes([0x68, 0x03, 0xC8, 0x00, 0x30, 0x00, 0x00, 0x58])
    reading = BLEAdvertisementDecoder.decode_meater_gatt(payload, mac="AA:BB:CC:DD:EE:FF")
    assert reading is not None
    assert abs(reading.internal_temp_c - 55.0) < 0.1
    assert abs(reading.internal_temp_f - 131.0) < 0.2
    assert reading.ambient_temp_c is not None
    assert abs(reading.ambient_temp_c - 115.25) < 0.2
    assert reading.battery_pct == 88
    assert reading.protocol == BLEProbeProtocol.MEATER
    assert "MEATER" in reading.probe_name


def test_decode_meater_cloud_dict():
    data = {
        "id": "probe_cloud_123",
        "temperature": {
            "internal": 57.5,
            "ambient": 110.0
        },
        "battery": 92
    }
    reading = BLEAdvertisementDecoder.decode_meater_cloud_dict(data)
    assert reading is not None
    assert abs(reading.internal_temp_c - 57.5) < 0.1
    assert reading.ambient_temp_c == 110.0
    assert reading.battery_pct == 92
    assert reading.protocol == BLEProbeProtocol.MEATER
    assert "Cloud" in reading.probe_name


def test_meater_adapters_and_composite_routing():
    wired = MockSensor(temp_f=225.0)
    meater_direct = MeaterBleClientAdapter(staleness_timeout_s=30.0)
    meater_cloud = MeaterCloudAdapter(staleness_timeout_s=60.0)

    composite = CompositeSensorAdapter(
        wired_sensor=wired,
        meater_direct_sensor=meater_direct,
        meater_cloud_sensor=meater_cloud,
        mode=MeatProbeMode.MEATER_BLE_DIRECT,
    )

    # 1. Pit is always wired
    pit_r = composite.read_temperature(SensorRole.PIT)
    assert pit_r.is_valid
    assert not pit_r.is_wireless

    # 2. Meater direct not yet connected: falls back to wired
    food_fb = composite.read_temperature(SensorRole.FOOD_1)
    assert food_fb.is_valid
    assert not food_fb.is_wireless

    # 3. Supply Meater GATT payload (55.0°C)
    gatt_bytes = bytes([0x68, 0x03, 0xC8, 0x00, 0x30, 0x00, 0x00, 0x58])
    assert meater_direct.process_gatt_payload(gatt_bytes, now_s=100.0)

    # In MeaterBleDirect mode, Food1 now returns Meater direct reading
    food_direct = composite.read_temperature(SensorRole.FOOD_1)
    assert food_direct.is_valid
    assert food_direct.is_wireless
    assert abs(food_direct.celsius - 55.0) < 0.1
    assert "MEATER (Direct)" in food_direct.probe_name

    # 4. Switch mode to MeaterCloud
    composite.set_mode(MeatProbeMode.MEATER_CLOUD)
    # Supply cloud payload (62.0°C)
    cloud_payload = {
        "devices": [
            {
                "id": "meater_cloud_abc",
                "temperature": {"internal": 62.0, "ambient": 125.0},
                "battery": 90,
            }
        ]
    }
    assert meater_cloud.process_cloud_payload(cloud_payload, now_s=100.0)

    food_cloud = composite.read_temperature(SensorRole.FOOD_1)
    assert food_cloud.is_valid
    assert food_cloud.is_wireless
    assert abs(food_cloud.celsius - 62.0) < 0.1
    assert "MEATER (Cloud)" in food_cloud.probe_name

    # 5. Switch mode to WiredOnly -> immediately returns wired sensor
    composite.set_mode(MeatProbeMode.WIRED_ONLY)
    food_wired = composite.read_temperature(SensorRole.FOOD_1)
    assert not food_wired.is_wireless
