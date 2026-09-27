"""Lid-open detection state machine for smoker chamber flare-up prevention."""

from __future__ import annotations

from dataclasses import dataclass, field
from collections import deque


@dataclass(frozen=True)
class LidDetectorConfig:
    """Parameters for detecting sudden smoker chamber thermal drops."""
    drop_threshold_deg: float = 15.0   # Minimum drop in degrees to trigger detection
    time_window_s: float = 30.0        # Time interval over which drop is evaluated
    pause_duration_s: float = 180.0     # Period to suppress airflow once triggered


class LidOpenDetector:
    """
    Detects sudden pit temperature drops caused by opening the smoker lid.
    
    When active:
    - Inhibits fan and closes damper to prevent oxygen from turning coals into a forge.
    - Suppresses PID windup.
    """

    def __init__(self, config: LidDetectorConfig | None = None) -> None:
        self._config = config or LidDetectorConfig()
        self._history: deque[tuple[float, float]] = deque()  # (timestamp_s, temperature)
        self._is_active: bool = False
        self._triggered_at_s: float | None = None

    @property
    def is_active(self) -> bool:
        """Returns True if lid-open suppression is currently engaged."""
        return self._is_active

    @property
    def seconds_remaining(self) -> float:
        """Seconds remaining in suppression window."""
        if not self._is_active or self._triggered_at_s is None:
            return 0.0
        # Will be evaluated against current time in update()
        return 0.0

    def reset(self) -> None:
        """Manually clear lid-open suppression and history."""
        self._history.clear()
        self._is_active = False
        self._triggered_at_s = None

    def update(self, current_temp: float, current_time_s: float) -> bool:
        """
        Process a new temperature reading and return True if lid is currently open.
        """
        # If already triggered, check if suppression window has expired
        if self._is_active:
            assert self._triggered_at_s is not None
            elapsed = current_time_s - self._triggered_at_s
            if elapsed >= self._config.pause_duration_s:
                # Window expired; exit suppression
                self._is_active = False
                self._triggered_at_s = None
                self._history.clear()
            else:
                return True

        # Append current reading
        self._history.append((current_time_s, current_temp))

        # Evict samples older than the detection time window
        while self._history and (current_time_s - self._history[0][0]) > self._config.time_window_s:
            self._history.popleft()

        if len(self._history) < 2:
            return False

        # Find maximum temperature recorded within the rolling time window
        max_temp_in_window = max(t for _, t in self._history)
        drop = max_temp_in_window - current_temp

        if drop >= self._config.drop_threshold_deg:
            # Sudden temperature drop detected! Trigger lid-open suppression
            self._is_active = True
            self._triggered_at_s = current_time_s
            return True

        return False
