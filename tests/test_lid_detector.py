"""Unit tests for LidOpenDetector domain state machine."""

import pytest
from esp32_fan_control.Domain.lid_detector import LidDetectorConfig, LidOpenDetector


def test_steady_temp_does_not_trigger_lid_open() -> None:
    detector = LidOpenDetector(LidDetectorConfig(drop_threshold_deg=15.0, time_window_s=30.0))

    assert detector.update(225.0, 0.0) is False
    assert detector.update(224.5, 5.0) is False
    assert detector.update(225.2, 10.0) is False
    assert detector.is_active is False


def test_slow_gradual_drop_does_not_trigger_lid_open() -> None:
    # 20° drop over 60 seconds (slower than 30s window)
    detector = LidOpenDetector(LidDetectorConfig(drop_threshold_deg=15.0, time_window_s=30.0))

    detector.update(225.0, 0.0)
    detector.update(215.0, 35.0)
    detector.update(205.0, 70.0)
    assert detector.is_active is False


def test_rapid_drop_triggers_lid_open_and_suppression_expires() -> None:
    detector = LidOpenDetector(
        LidDetectorConfig(drop_threshold_deg=15.0, time_window_s=30.0, pause_duration_s=180.0)
    )

    detector.update(225.0, 0.0)
    # Rapid drop from 225°F to 200°F (25° drop in 10s)
    triggered = detector.update(200.0, 10.0)
    assert triggered is True
    assert detector.is_active is True

    # Stays active during the 180s pause window
    assert detector.update(205.0, 100.0) is True
    assert detector.is_active is True

    # Exceeds the 180s pause window (10.0 + 180.0 = 190.0)
    assert detector.update(210.0, 195.0) is False
    assert detector.is_active is False
