"""Unit tests for TemperatureReading and SensorRole domain models."""

import pytest
from esp32_fan_control.Domain.temperature import SensorFault, SensorRole, TemperatureReading


def test_temperature_reading_conversions() -> None:
    # 100°C = 212°F
    reading = TemperatureReading(celsius=100.0, role=SensorRole.PIT, timestamp_s=10.0)
    assert reading.celsius == 100.0
    assert pytest.approx(reading.fahrenheit, 0.01) == 212.0
    assert reading.is_valid is True

    # 225°F from factory constructor
    reading_f = TemperatureReading.from_fahrenheit(225.0, role=SensorRole.PIT, timestamp_s=15.0)
    assert pytest.approx(reading_f.fahrenheit, 0.01) == 225.0
    assert pytest.approx(reading_f.celsius, 0.01) == 107.22
    assert reading_f.is_valid is True


def test_sensor_fault_validity() -> None:
    healthy = TemperatureReading.from_fahrenheit(225.0, role=SensorRole.PIT, timestamp_s=1.0)
    assert healthy.is_valid is True

    disconnected = TemperatureReading.from_fahrenheit(
        225.0, role=SensorRole.PIT, timestamp_s=1.0, fault=SensorFault.DISCONNECTED
    )
    assert disconnected.is_valid is False

    short_vcc = TemperatureReading.from_fahrenheit(
        225.0, role=SensorRole.PIT, timestamp_s=1.0, fault=SensorFault.SHORT_TO_VCC
    )
    assert short_vcc.is_valid is False

    short_gnd = TemperatureReading.from_fahrenheit(
        225.0, role=SensorRole.PIT, timestamp_s=1.0, fault=SensorFault.SHORT_TO_GND
    )
    assert short_gnd.is_valid is False

    out_of_range = TemperatureReading(
        celsius=600.0, role=SensorRole.PIT, timestamp_s=1.0, fault=SensorFault.OK
    )
    assert out_of_range.is_valid is False
