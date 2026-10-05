"""Bounded in-process control exchange; only the control owner mutates the service."""
from collections import deque
from threading import Lock

from esp32_fan_control.Domain.control import ControlCommand, ControlState


class ThreadedControlChannel:
    capacity = 8

    def __init__(self) -> None:
        self._commands: deque[ControlCommand] = deque()
        self._state: ControlState | None = None
        self._lock = Lock()

    def submit(self, command: ControlCommand) -> bool:
        with self._lock:
            if len(self._commands) >= self.capacity:
                return False
            self._commands.append(command)
            return True

    def receive(self) -> ControlCommand | None:
        with self._lock:
            return self._commands.popleft() if self._commands else None

    def publish(self, state: ControlState) -> None:
        # Domain messages and their nested values are frozen dataclasses.
        with self._lock:
            self._state = state

    def snapshot(self) -> ControlState | None:
        with self._lock:
            return self._state
