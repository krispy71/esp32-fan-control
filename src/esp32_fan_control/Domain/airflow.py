"""Airflow demand, actuator target entities, and dual-stage coordinator."""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class AirflowDemand:
    """Calculated airflow demand percentage from PID controller (0.0% to 100.0%)."""
    value_pct: float

    def __post_init__(self) -> None:
        if not (0.0 <= self.value_pct <= 100.0):
            raise ValueError(f"AirflowDemand must be in range [0.0, 100.0], got {self.value_pct}")


@dataclass(frozen=True)
class ActuatorTargets:
    """Target setpoints for physical damper and blower fan."""
    damper_position_pct: float
    blower_speed_pct: float

    def __post_init__(self) -> None:
        if not (0.0 <= self.damper_position_pct <= 100.0):
            raise ValueError(f"damper_position_pct must be in [0.0, 100.0], got {self.damper_position_pct}")
        if not (0.0 <= self.blower_speed_pct <= 100.0):
            raise ValueError(f"blower_speed_pct must be in [0.0, 100.0], got {self.blower_speed_pct}")


class ActuatorCoordinator:
    """
    Coordinates servo-damper aperture and blower speed from aggregate airflow demand.
    
    Inspired by HeaterMeter & Roto-Damper sequential modulation:
    - Stage 1 (0% to Threshold, e.g. 40%): Natural Draft.
      The damper modulates from 0% to 100% open while the blower fan remains OFF.
    - Stage 2 (Above Threshold): Forced Air Boost.
      The damper remains 100% open while the blower fan modulates from min speed to 100%.
    - At 0% Demand:
      Both damper and fan are 0% (fully closed to stop natural convective draft).
    """

    def __init__(
        self,
        blower_threshold_pct: float = 40.0,
        min_blower_speed_pct: float = 10.0,
    ) -> None:
        if not (0.0 < blower_threshold_pct < 100.0):
            raise ValueError(f"blower_threshold_pct must be in range (0.0, 100.0), got {blower_threshold_pct}")
        if not (0.0 <= min_blower_speed_pct < 100.0):
            raise ValueError(f"min_blower_speed_pct must be in range [0.0, 100.0), got {min_blower_speed_pct}")

        self._threshold = blower_threshold_pct
        self._min_blower = min_blower_speed_pct

    @property
    def blower_threshold_pct(self) -> float:
        return self._threshold

    @property
    def min_blower_speed_pct(self) -> float:
        return self._min_blower

    def coordinate(self, demand: AirflowDemand) -> ActuatorTargets:
        """Translate abstract demand into concrete damper and blower setpoints."""
        val = demand.value_pct

        if val <= 0.0:
            return ActuatorTargets(damper_position_pct=0.0, blower_speed_pct=0.0)

        if val <= self._threshold:
            # Stage 1: Natural draft modulation via damper only
            damper = (val / self._threshold) * 100.0
            return ActuatorTargets(
                damper_position_pct=round(min(100.0, max(0.0, damper)), 2),
                blower_speed_pct=0.0,
            )

        # Stage 2: Damper locked 100% open; blower modulates from min_blower to 100%
        span = 100.0 - self._threshold
        excess = val - self._threshold
        ratio = excess / span
        blower = self._min_blower + ratio * (100.0 - self._min_blower)

        return ActuatorTargets(
            damper_position_pct=100.0,
            blower_speed_pct=round(min(100.0, max(0.0, blower)), 2),
        )
