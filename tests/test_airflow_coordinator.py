"""Unit tests for ActuatorCoordinator dual-stage airflow modulation."""

import pytest
from esp32_fan_control.Domain.airflow import ActuatorCoordinator, AirflowDemand


def test_airflow_demand_bounds() -> None:
    valid = AirflowDemand(50.0)
    assert valid.value_pct == 50.0

    with pytest.raises(ValueError):
        AirflowDemand(-0.1)

    with pytest.raises(ValueError):
        AirflowDemand(100.1)


def test_coordinator_zero_demand_closes_both_actuators() -> None:
    coordinator = ActuatorCoordinator(blower_threshold_pct=40.0, min_blower_speed_pct=10.0)
    targets = coordinator.coordinate(AirflowDemand(0.0))

    # Invariant: At 0% demand, damper must be 0% to prevent chimney draft, fan 0%
    assert targets.damper_position_pct == 0.0
    assert targets.blower_speed_pct == 0.0


def test_coordinator_stage_one_natural_draft() -> None:
    coordinator = ActuatorCoordinator(blower_threshold_pct=40.0, min_blower_speed_pct=10.0)

    # 20% demand is halfway through the 40% threshold -> damper should be 50%, blower OFF
    targets_20 = coordinator.coordinate(AirflowDemand(20.0))
    assert targets_20.damper_position_pct == 50.0
    assert targets_20.blower_speed_pct == 0.0

    # 40% demand reaches the threshold -> damper 100%, blower still 0%
    targets_40 = coordinator.coordinate(AirflowDemand(40.0))
    assert targets_40.damper_position_pct == 100.0
    assert targets_40.blower_speed_pct == 0.0


def test_coordinator_stage_two_forced_draft_boost() -> None:
    coordinator = ActuatorCoordinator(blower_threshold_pct=40.0, min_blower_speed_pct=10.0)

    # 70% demand is halfway between 40% and 100% -> damper 100%, blower halfway between 10% and 100% = 55%
    targets_70 = coordinator.coordinate(AirflowDemand(70.0))
    assert targets_70.damper_position_pct == 100.0
    assert targets_70.blower_speed_pct == 55.0

    # 100% demand -> damper 100%, blower 100%
    targets_100 = coordinator.coordinate(AirflowDemand(100.0))
    assert targets_100.damper_position_pct == 100.0
    assert targets_100.blower_speed_pct == 100.0


def test_coordinator_invalid_config() -> None:
    with pytest.raises(ValueError):
        ActuatorCoordinator(blower_threshold_pct=0.0)

    with pytest.raises(ValueError):
        ActuatorCoordinator(blower_threshold_pct=100.0)

    with pytest.raises(ValueError):
        ActuatorCoordinator(min_blower_speed_pct=-1.0)
