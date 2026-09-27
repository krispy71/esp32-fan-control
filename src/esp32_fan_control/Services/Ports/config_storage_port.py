"""Configuration storage persistence port."""

from __future__ import annotations

from typing import Protocol
from esp32_fan_control.Domain.configuration import SmokerConfig


class ConfigStoragePort(Protocol):
    """Port interface for persisting and restoring configuration across reboots."""

    def load_config(self) -> SmokerConfig | None:
        """Load stored configuration, or return None if not present or corrupt."""
        ...

    def save_config(self, config: SmokerConfig) -> bool:
        """Save configuration to non-volatile storage. Returns True on success."""
        ...
