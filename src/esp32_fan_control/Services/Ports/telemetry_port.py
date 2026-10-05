"""Telemetry publishing boundary."""

from typing import Protocol
from esp32_fan_control.Domain.telemetry import TelemetrySnapshot


class TelemetryPublisherPort(Protocol):
    """Port for broadcasting telemetry over WebSocket, MQTT, Serial, or REST."""

    def publish(self, snapshot: TelemetrySnapshot) -> None:
        """Publish telemetry snapshot to external consumers."""
        ...
