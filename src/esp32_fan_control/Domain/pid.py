"""Discrete PID regulator with anti-windup clamping and derivative filtering."""

from __future__ import annotations

from dataclasses import dataclass

from esp32_fan_control.Domain.airflow import AirflowDemand


@dataclass(frozen=True)
class PIDConfig:
    """Tuning parameters and boundary clamps for PID control loop."""
    kp: float = 3.0
    ki: float = 0.02
    kd: float = 15.0
    integral_min: float = 0.0
    integral_max: float = 100.0
    output_min: float = 0.0
    output_max: float = 100.0
    derivative_filter_alpha: float = 0.8  # Exponential filter on derivative term (0.0 to 1.0)


class PIDRegulator:
    """
    Closed-loop PID controller designed for thermal combustion systems.
    
    Invariants:
    - Integral windup is prevented by explicit clamping.
    - Output is strictly clamped between 0.0% and 100.0%.
    - Safe handling of sample time intervals.
    """

    def __init__(self, target_setpoint: float, config: PIDConfig | None = None) -> None:
        self._setpoint = target_setpoint
        self._config = config or PIDConfig()
        self._integral: float = 0.0
        self._last_error: float = 0.0
        self._last_time_s: float | None = None
        self._filtered_derivative: float = 0.0

    @property
    def setpoint(self) -> float:
        return self._setpoint

    @setpoint.setter
    def setpoint(self, value: float) -> None:
        self._setpoint = value

    @property
    def config(self) -> PIDConfig:
        return self._config

    @config.setter
    def config(self, value: PIDConfig) -> None:
        self._config = value

    @property
    def integral(self) -> float:
        return self._integral

    def reset(self) -> None:
        """Reset internal accumulator and state."""
        self._integral = 0.0
        self._last_error = 0.0
        self._last_time_s = None
        self._filtered_derivative = 0.0

    def suspend(self) -> None:
        """Keep accumulated bias, excluding inhibited time and derivative changes."""
        self._last_time_s = None
        self._filtered_derivative = 0.0

    def compute(self, current_temp: float, current_time_s: float) -> AirflowDemand:
        """Calculate airflow demand from temperature error."""
        error = self._setpoint - current_temp

        if self._last_time_s is None:
            # First tick initializes state
            self._last_time_s = current_time_s
            self._last_error = error
            p_term = self._config.kp * error
            output = max(self._config.output_min, min(self._config.output_max, p_term + self._config.ki * self._integral))
            return AirflowDemand(value_pct=round(output, 2))

        dt = current_time_s - self._last_time_s
        if dt <= 0.0:
            dt = 1e-3  # Guard against division by zero

        # 1. Proportional Term
        p_term = self._config.kp * error

        # 2. Integral Term with Anti-Windup Clamping
        candidate_integral = max(
            self._config.integral_min,
            min(self._config.integral_max, self._integral + error * dt),
        )
        i_term = self._config.ki * candidate_integral

        # 3. Derivative Term with Low-Pass Filtering
        raw_derivative = (error - self._last_error) / dt
        alpha = self._config.derivative_filter_alpha
        self._filtered_derivative = (alpha * self._filtered_derivative) + ((1.0 - alpha) * raw_derivative)
        d_term = self._config.kd * self._filtered_derivative

        # 4. Total Output with Saturation Clamping
        total = p_term + i_term + d_term
        if not ((total > self._config.output_max and error > 0.0)
                or (total < self._config.output_min and error < 0.0)):
            self._integral = candidate_integral
        clamped = max(self._config.output_min, min(self._config.output_max, total))

        # Update historical state
        self._last_error = error
        self._last_time_s = current_time_s

        return AirflowDemand(value_pct=round(clamped, 2))
