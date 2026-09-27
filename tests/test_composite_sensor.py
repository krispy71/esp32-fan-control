"""Tests for CompositeSensorAdapter multiplexing wired and wireless sensors."""

import pytest
from esp32_fan_control.Adapters.ble_probe_adapter import BLEProbeAdapter
from esp32_fan_control.Adapters.composite_sensor_adapter import CompositeSensorAdapter
from esp32_fan_control.Domain.temperature import SensorFault, SensorRole, TemperatureReading
from esp32_fan_control.Services.Ports.sensor_port import TemperatureSensorPort


class MockWiredSensor(TemperatureSensorPort):
    def __init__(self, pit_f: float = 225.0, food_f: float = 68.0) -> None:
        self.pit_f = pit_f
        self.food_f = food_f
        self.fault = SensorFault.OK

    def read_temperature(self, role: SensorRole) -> TemperatureReading:
        if role == SensorRole.PIT:
            return TemperatureReading.from_fahrenheit(self.pit_f, role, 100.0, fault=self.fault)
        return TemperatureReading.from_fahrenheit(self.food_f, role, 100.0, fault=SensorFault.OK)


def test_composite_sensor_pit_always_wired():
    wired = MockWiredSensor(pit_f=250.0, food_f=70.0)
    ble = BLEProbeAdapter()
    ble.simulate_reading(internal_temp_c=50.0, ambient_temp_c=120.0, now_s=100.0)

    composite = CompositeSensorAdapter(wired_sensor=wired, wireless_sensor=ble)

    pit_reading = composite.read_temperature(SensorRole.PIT)
    assert pit_reading.is_valid
    assert pytest.approx(pit_reading.fahrenheit, rel=1e-3) == 250.0
    assert not pit_reading.is_wireless


def test_composite_sensor_food_prefers_wireless():
    wired = MockWiredSensor(pit_f=225.0, food_f=65.0)
    ble = BLEProbeAdapter(staleness_timeout_s=30.0)
    ble.simulate_reading(
        internal_temp_c=57.22, # 135°F
        ambient_temp_c=107.22,
        battery_pct=85,
        probe_name="Inkbird Probe",
        now_s=100.0,
    )

    composite = CompositeSensorAdapter(wired_sensor=wired, wireless_sensor=ble)

    food_reading = composite.read_temperature(SensorRole.FOOD_1)
    assert food_reading.is_valid
    assert food_reading.is_wireless
    assert pytest.approx(food_reading.fahrenheit, abs=0.2) == 135.0
    assert food_reading.battery_pct == 85
    assert food_reading.probe_name == "Inkbird Probe"


def test_composite_sensor_fallback_when_wireless_disconnected():
    wired = MockWiredSensor(pit_f=225.0, food_f=68.0)
    ble = BLEProbeAdapter(staleness_timeout_s=30.0)
    # No packets received by BLE probe

    composite = CompositeSensorAdapter(wired_sensor=wired, wireless_sensor=ble)

    # Food probe should fallback to wired sensor
    food_reading = composite.read_temperature(SensorRole.FOOD_1)
    assert food_reading.is_valid
    assert not food_reading.is_wireless
    assert pytest.approx(food_reading.fahrenheit, rel=1e-3) == 68.0


def test_composite_sensor_fallback_on_staleness():
    wired = MockWiredSensor(pit_f=225.0, food_f=72.0)
    ble = BLEProbeAdapter(staleness_timeout_s=30.0)
    ble.simulate_reading(internal_temp_c=55.0, now_s=100.0)

    composite = CompositeSensorAdapter(wired_sensor=wired, wireless_sensor=ble)

    # Active wireless at t=110s
    ble.set_mock_time(110.0)
    food_active = composite.read_temperature(SensorRole.FOOD_1)
    assert food_active.is_wireless

    # Stale at t=145s (45s > 30s) -> fallback to wired
    ble.set_mock_time(145.0)
    food_fallback = composite.read_temperature(SensorRole.FOOD_1)
    assert food_fallback.is_valid
    assert not food_fallback.is_wireless
    assert pytest.approx(food_fallback.fahrenheit, rel=1e-3) == 72.0
