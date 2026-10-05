"""Tests for Inland E-Ink screen adapter, DisplayPort, and DisplayView domain model."""

from __future__ import annotations

import pytest

from esp32_fan_control.Domain.display_view import DisplayView
from esp32_fan_control.Services.Ports.telemetry_port import TelemetrySnapshot
from esp32_fan_control.Adapters.eink_display_adapter import (
    InlandEInkAdapter,
    EInkModel,
    COLOR_BLACK,
    COLOR_WHITE,
)


def test_display_view_formatting() -> None:
    snapshot = TelemetrySnapshot(
        timestamp_s=120.0,
        pit_temp_f=225.4,
        meat_temp_f=145.2,
        setpoint_f=225.0,
        damper_position_pct=45.0,
        blower_speed_pct=20.0,
        demand_pct=25.0,
        lid_open=False,
        status="HOLDING_SETPOINT",
        is_meat_wireless=True,
        meat_battery_pct=92,
        meat_probe_name="MEATER_1A2B",
    )
    view = DisplayView.from_telemetry(snapshot)
    assert view.pit_valid is True
    assert view.meat_valid is True
    assert view.format_pit() == "225.4 F"
    assert view.format_meat() == "145.2 F"
    assert view.format_setpoint() == "Set: 225 F"
    assert view.is_meat_wireless is True
    assert view.meat_battery_pct == 92
    assert view.meat_probe_name == "MEATER_1A2B"


def test_display_view_invalid_sensors() -> None:
    snapshot = TelemetrySnapshot(
        timestamp_s=0.0,
        pit_temp_f=None,
        meat_temp_f=None,
        setpoint_f=250.0,
        damper_position_pct=0.0,
        blower_speed_pct=0.0,
        demand_pct=0.0,
        lid_open=True,
        status="SENSOR_FAULT",
    )
    view = DisplayView.from_telemetry(snapshot)
    assert view.pit_valid is False
    assert view.meat_valid is False
    assert view.format_pit() == "ERR"
    assert view.format_meat() == "--.- F"
    assert view.lid_open is True


def test_display_view_significant_change_detection() -> None:
    base = DisplayView(
        pit_temp_f=225.0,
        meat_temp_f=140.0,
        setpoint_f=225.0,
        damper_pct=50.0,
        blower_pct=30.0,
        lid_open=False,
        status="STABLE",
    )

    # First view always triggers
    assert base.has_significant_change(None) is True

    # Negligible temperature change (< 0.5 F)
    slight_drift = DisplayView(
        pit_temp_f=225.2,
        meat_temp_f=140.1,
        setpoint_f=225.0,
        damper_pct=51.0,
        blower_pct=31.0,
        lid_open=False,
        status="STABLE",
    )
    assert slight_drift.has_significant_change(base) is False

    # Significant pit temp change (>= 0.5 F)
    pit_jump = DisplayView(
        pit_temp_f=225.8,
        meat_temp_f=140.1,
        setpoint_f=225.0,
        damper_pct=50.0,
        blower_pct=30.0,
        lid_open=False,
        status="STABLE",
    )
    assert pit_jump.has_significant_change(base) is True

    # Actuator duty shift (>= 5%)
    fan_jump = DisplayView(
        pit_temp_f=225.2,
        meat_temp_f=140.1,
        setpoint_f=225.0,
        damper_pct=50.0,
        blower_pct=40.0,
        lid_open=False,
        status="STABLE",
    )
    assert fan_jump.has_significant_change(base) is True

    # Lid open state transition
    lid_flip = DisplayView(
        pit_temp_f=225.0,
        meat_temp_f=140.0,
        setpoint_f=225.0,
        damper_pct=50.0,
        blower_pct=30.0,
        lid_open=True,
        status="STABLE",
    )
    assert lid_flip.has_significant_change(base) is True

    # Status change
    status_change = DisplayView(
        pit_temp_f=225.0,
        meat_temp_f=140.0,
        setpoint_f=225.0,
        damper_pct=50.0,
        blower_pct=30.0,
        lid_open=False,
        status="RECOVERY",
    )
    assert status_change.has_significant_change(base) is True


def test_inland_eink_adapter_drawing_primitives() -> None:
    adapter = InlandEInkAdapter(EInkModel.INLAND_2_13_INCH)
    adapter.begin()
    assert adapter.width == 250
    assert adapter.height == 122

    # Initially white
    assert adapter.get_pixel(10, 10) == COLOR_WHITE

    # Draw pixel
    adapter.draw_pixel(10, 10, COLOR_BLACK)
    assert adapter.get_pixel(10, 10) == COLOR_BLACK
    assert adapter.get_pixel(11, 10) == COLOR_WHITE

    # Draw rect and fill rect
    adapter.fill_rect(20, 20, 10, 10, COLOR_BLACK)
    for y in range(20, 30):
        for x in range(20, 30):
            assert adapter.get_pixel(x, y) == COLOR_BLACK

    # Clear buffer
    adapter.clear_buffer(COLOR_WHITE)
    assert adapter.get_pixel(25, 25) == COLOR_WHITE


def test_inland_eink_render_layout_and_ascii() -> None:
    adapter = InlandEInkAdapter(EInkModel.INLAND_2_13_INCH)
    adapter.begin()

    view = DisplayView(
        pit_temp_f=225.0,
        meat_temp_f=145.0,
        setpoint_f=225.0,
        damper_pct=50.0,
        blower_pct=35.0,
        lid_open=False,
        status="REGULATING",
        is_meat_wireless=True,
        meat_battery_pct=88,
        meat_probe_name="MEATER+",
    )

    adapter.render(view)
    assert adapter.refresh_count == 1

    # Buffer should contain black pixels for text and graphics
    black_pixel_count = sum(
        1
        for y in range(adapter.height)
        for x in range(adapter.width)
        if adapter.get_pixel(x, y) == COLOR_BLACK
    )
    assert black_pixel_count > 200

    # Test ascii export
    ascii_out = adapter.dump_ascii(step=4)
    assert len(ascii_out) > 0
    assert "#" in ascii_out
    assert "." in ascii_out


def test_inland_eink_smart_refresh_scheduler() -> None:
    adapter = InlandEInkAdapter(
        EInkModel.INLAND_2_13_INCH,
        min_refresh_interval_s=10.0,
        heartbeat_interval_s=30.0,
    )
    adapter.begin()

    v1 = DisplayView(
        pit_temp_f=225.0,
        meat_temp_f=140.0,
        setpoint_f=225.0,
        status="STABLE",
    )

    # 1. First update triggers refresh
    assert adapter.update(100.0, v1) is True
    assert adapter.refresh_count == 1

    # 2. Update within 10s cooldown does NOT refresh even if change occurs
    v_change = DisplayView(
        pit_temp_f=235.0,
        meat_temp_f=140.0,
        setpoint_f=225.0,
        status="STABLE",
    )
    assert adapter.update(105.0, v_change) is False
    assert adapter.refresh_count == 1

    # 3. Update after cooldown (12s later) with no significant change does NOT refresh
    assert adapter.update(112.0, v1) is False
    assert adapter.refresh_count == 1

    # 4. Update after cooldown with significant change triggers refresh
    assert adapter.update(112.0, v_change) is True
    assert adapter.refresh_count == 2

    # 5. Heartbeat refresh triggers after 30s even with identical view
    assert adapter.update(125.0, v_change) is False
    assert adapter.update(143.0, v_change) is True
    assert adapter.refresh_count == 3


def test_inland_eink_telemetry_publisher_integration() -> None:
    adapter = InlandEInkAdapter(EInkModel.INLAND_2_13_INCH)
    adapter.begin()

    snapshot = TelemetrySnapshot(
        timestamp_s=50.0,
        pit_temp_f=224.8,
        meat_temp_f=130.0,
        setpoint_f=225.0,
        damper_position_pct=15.0,
        blower_speed_pct=0.0,
        demand_pct=15.0,
        lid_open=False,
        status="HOLDING",
    )

    adapter.publish(snapshot)
    assert adapter.refresh_count == 1

    # Low power sleep
    assert adapter.is_sleeping is False
    adapter.sleep()
    assert adapter.is_sleeping is True
