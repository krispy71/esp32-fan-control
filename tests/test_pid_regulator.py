"""Unit tests for discrete PID regulator."""

from esp32_fan_control.Domain.pid import PIDConfig, PIDRegulator


def test_pid_proportional_response() -> None:
    config = PIDConfig(kp=2.0, ki=0.0, kd=0.0, output_min=0.0, output_max=100.0)
    pid = PIDRegulator(target_setpoint=225.0, config=config)

    # 10 degrees below setpoint -> 10 * 2.0 = 20% demand
    demand = pid.compute(current_temp=215.0, current_time_s=1.0)
    assert demand.value_pct == 20.0

    # Over temperature -> 0% demand (clamped)
    demand_over = pid.compute(current_temp=235.0, current_time_s=2.0)
    assert demand_over.value_pct == 0.0


def test_pid_anti_windup_clamping() -> None:
    config = PIDConfig(kp=1.0, ki=0.5, kd=0.0, integral_min=0.0, integral_max=50.0)
    pid = PIDRegulator(target_setpoint=225.0, config=config)

    # Cold pit for a long time (100 seconds) with 25° error
    pid.compute(current_temp=200.0, current_time_s=0.0)
    demand = pid.compute(current_temp=200.0, current_time_s=100.0)

    # Integral should be clamped at 50.0 rather than winding up to 2500
    assert pid.integral == 50.0
    assert demand.value_pct <= 100.0


def test_pid_reset() -> None:
    pid = PIDRegulator(target_setpoint=225.0)
    pid.compute(current_temp=200.0, current_time_s=1.0)
    pid.compute(current_temp=200.0, current_time_s=2.0)
    assert pid.integral > 0.0

    pid.reset()
    assert pid.integral == 0.0


def test_pid_suspend_excludes_elapsed_pause_and_derivative_kick() -> None:
    pid = PIDRegulator(225, PIDConfig(kp=0, ki=0.1, kd=15))
    pid.compute(220, 0)
    assert pid.compute(220, 1).value_pct == 0.5
    integral = pid.integral
    pid.suspend()
    # Both the 180-second interval and changed error are excluded on recovery.
    assert pid.compute(200, 181).value_pct == 0.5
    assert pid.integral == integral
    assert pid.compute(200, 182).value_pct == 3.0


def test_pid_does_not_integrate_further_into_output_saturation() -> None:
    pid = PIDRegulator(225, PIDConfig(kp=10, ki=1, kd=0))
    pid.compute(200, 0)
    assert pid.compute(200, 1).value_pct == 100
    assert pid.integral == 0


def test_integral_driven_saturation_still_commands_the_output_limit() -> None:
    pid = PIDRegulator(225, PIDConfig(kp=0, ki=10, kd=0))
    pid.compute(200, 0)
    assert pid.compute(200, 1).value_pct == 100
    assert pid.integral == 0
