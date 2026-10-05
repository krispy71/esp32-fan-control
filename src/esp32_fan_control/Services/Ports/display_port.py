"""Display port contract for visual feedback devices."""

from __future__ import annotations

from typing import Protocol

from esp32_fan_control.Domain.display_view import DisplayView


class DisplayPort(Protocol):
    """Port for rendering status views on hardware or simulated displays."""

    def begin(self) -> None:
        """Initialize display controller, bus, and clear frame buffer."""
        ...

    def render(self, view: DisplayView, force_full: bool = False) -> None:
        """Render DisplayView onto screen buffer and trigger display update."""
        ...

    def clear(self) -> None:
        """Clear display buffer to white/blank and flush to hardware."""
        ...

    def sleep(self) -> None:
        """Put the display panel into ultra-low-power sleep."""
        ...

    def is_busy(self) -> bool:
        """Return True if display is currently refreshing and unable to accept commands."""
        ...
