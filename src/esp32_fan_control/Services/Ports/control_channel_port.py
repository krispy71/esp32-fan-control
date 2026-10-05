"""Bounded, thread-safe command and snapshot handoff."""

from typing import Protocol

from esp32_fan_control.Domain.control import ControlCommand, ControlState


class ControlChannelPort(Protocol):
    capacity: int = 8

    def submit(self, command: ControlCommand) -> bool:
        """Nonblocking FIFO enqueue; False if full/unavailable."""
        ...

    def receive(self) -> ControlCommand | None:
        """Single control-task consumer; None if no command is available."""
        ...

    def publish(self, state: ControlState) -> None:
        """Atomically publish an immutable, coherent snapshot."""
        ...

    def snapshot(self) -> ControlState | None:
        """Return the latest immutable state; None before initialization."""
        ...
