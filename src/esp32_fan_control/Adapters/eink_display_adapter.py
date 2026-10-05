"""Inland E-Ink / E-Paper display adapter for ESP32 and desktop simulation."""

from __future__ import annotations

import enum
from typing import Final

from esp32_fan_control.Domain.display_view import DisplayView
from esp32_fan_control.Services.Ports.display_port import DisplayPort
from esp32_fan_control.Services.Ports.telemetry_port import TelemetryPublisherPort, TelemetrySnapshot


class EInkModel(enum.Enum):
    """Supported Inland E-Ink screen resolutions."""
    INLAND_2_13_INCH = (250, 122)
    INLAND_1_54_INCH = (200, 200)


COLOR_BLACK: Final[int] = 0
COLOR_WHITE: Final[int] = 1

# Standard 5x7 ASCII font table (ASCII 32 to 127). Column-major format.
FONT_5X7: Final[dict[int, tuple[int, ...]]] = {
    32: (0x00, 0x00, 0x00, 0x00, 0x00),  # Space
    33: (0x00, 0x00, 0x5F, 0x00, 0x00),  # !
    34: (0x00, 0x07, 0x00, 0x07, 0x00),  # "
    35: (0x14, 0x7F, 0x14, 0x7F, 0x14),  # #
    36: (0x24, 0x2A, 0x7F, 0x2A, 0x12),  # $
    37: (0x23, 0x13, 0x08, 0x64, 0x62),  # %
    38: (0x36, 0x49, 0x55, 0x22, 0x50),  # &
    39: (0x00, 0x05, 0x03, 0x00, 0x00),  # '
    40: (0x00, 0x1C, 0x22, 0x41, 0x00),  # (
    41: (0x00, 0x41, 0x22, 0x1C, 0x00),  # )
    42: (0x14, 0x08, 0x3E, 0x08, 0x14),  # *
    43: (0x08, 0x08, 0x3E, 0x08, 0x08),  # +
    44: (0x00, 0x50, 0x30, 0x00, 0x00),  # ,
    45: (0x08, 0x08, 0x08, 0x08, 0x08),  # -
    46: (0x00, 0x60, 0x60, 0x00, 0x00),  # .
    47: (0x20, 0x10, 0x08, 0x04, 0x02),  # /
    48: (0x3E, 0x51, 0x49, 0x45, 0x3E),  # 0
    49: (0x00, 0x42, 0x7F, 0x40, 0x00),  # 1
    50: (0x42, 0x61, 0x51, 0x49, 0x46),  # 2
    51: (0x21, 0x41, 0x45, 0x4B, 0x31),  # 3
    52: (0x18, 0x14, 0x12, 0x7F, 0x10),  # 4
    53: (0x27, 0x45, 0x45, 0x45, 0x39),  # 5
    54: (0x3C, 0x4A, 0x49, 0x49, 0x30),  # 6
    55: (0x01, 0x71, 0x09, 0x05, 0x03),  # 7
    56: (0x36, 0x49, 0x49, 0x49, 0x36),  # 8
    57: (0x06, 0x49, 0x49, 0x29, 0x1E),  # 9
    58: (0x00, 0x36, 0x36, 0x00, 0x00),  # :
    59: (0x00, 0x56, 0x36, 0x00, 0x00),  # ;
    60: (0x08, 0x14, 0x22, 0x41, 0x00),  # <
    61: (0x14, 0x14, 0x14, 0x14, 0x14),  # =
    62: (0x00, 0x41, 0x22, 0x14, 0x08),  # >
    63: (0x02, 0x01, 0x51, 0x09, 0x06),  # ?
    64: (0x32, 0x49, 0x79, 0x41, 0x3E),  # @
    65: (0x7E, 0x11, 0x11, 0x11, 0x7E),  # A
    66: (0x7F, 0x49, 0x49, 0x49, 0x36),  # B
    67: (0x3E, 0x41, 0x41, 0x41, 0x22),  # C
    68: (0x7F, 0x41, 0x41, 0x22, 0x1C),  # D
    69: (0x7F, 0x49, 0x49, 0x49, 0x41),  # E
    70: (0x7F, 0x09, 0x09, 0x09, 0x01),  # F
    71: (0x3E, 0x41, 0x49, 0x49, 0x7A),  # G
    72: (0x7F, 0x08, 0x08, 0x08, 0x7F),  # H
    73: (0x00, 0x41, 0x7F, 0x41, 0x00),  # I
    74: (0x20, 0x40, 0x41, 0x3F, 0x01),  # J
    75: (0x7F, 0x08, 0x14, 0x22, 0x41),  # K
    76: (0x7F, 0x40, 0x40, 0x40, 0x40),  # L
    77: (0x7F, 0x02, 0x0C, 0x02, 0x7F),  # M
    78: (0x7F, 0x04, 0x08, 0x10, 0x7F),  # N
    79: (0x3E, 0x41, 0x41, 0x41, 0x3E),  # O
    80: (0x7F, 0x09, 0x09, 0x09, 0x06),  # P
    81: (0x3E, 0x41, 0x51, 0x21, 0x5E),  # Q
    82: (0x7F, 0x09, 0x19, 0x29, 0x46),  # R
    83: (0x46, 0x49, 0x49, 0x49, 0x31),  # S
    84: (0x01, 0x01, 0x7F, 0x01, 0x01),  # T
    85: (0x3F, 0x40, 0x40, 0x40, 0x3F),  # U
    86: (0x1F, 0x20, 0x40, 0x20, 0x1F),  # V
    87: (0x3F, 0x40, 0x38, 0x40, 0x3F),  # W
    88: (0x63, 0x14, 0x08, 0x14, 0x63),  # X
    89: (0x07, 0x08, 0x70, 0x08, 0x07),  # Y
    90: (0x61, 0x51, 0x49, 0x45, 0x43),  # Z
    91: (0x00, 0x7F, 0x41, 0x41, 0x00),  # [
    92: (0x02, 0x04, 0x08, 0x10, 0x20),  # \
    93: (0x00, 0x41, 0x41, 0x7F, 0x00),  # ]
    94: (0x04, 0x02, 0x01, 0x02, 0x04),  # ^
    95: (0x40, 0x40, 0x40, 0x40, 0x40),  # _
    96: (0x00, 0x01, 0x02, 0x04, 0x00),  # `
    97: (0x20, 0x54, 0x54, 0x54, 0x78),  # a
    98: (0x7F, 0x48, 0x44, 0x44, 0x38),  # b
    99: (0x38, 0x44, 0x44, 0x44, 0x20),  # c
    100: (0x38, 0x44, 0x44, 0x48, 0x7F),  # d
    101: (0x38, 0x54, 0x54, 0x54, 0x18),  # e
    102: (0x08, 0x7E, 0x09, 0x01, 0x02),  # f
    103: (0x0C, 0x52, 0x52, 0x52, 0x3E),  # g
    104: (0x7F, 0x08, 0x04, 0x04, 0x78),  # h
    105: (0x00, 0x44, 0x7D, 0x40, 0x00),  # i
    106: (0x20, 0x40, 0x44, 0x3D, 0x00),  # j
    107: (0x7F, 0x10, 0x28, 0x44, 0x00),  # k
    108: (0x00, 0x41, 0x7F, 0x40, 0x00),  # l
    109: (0x7C, 0x04, 0x18, 0x04, 0x78),  # m
    110: (0x7C, 0x08, 0x04, 0x04, 0x78),  # n
    111: (0x38, 0x44, 0x44, 0x44, 0x38),  # o
    112: (0x7C, 0x14, 0x14, 0x14, 0x08),  # p
    113: (0x08, 0x14, 0x14, 0x18, 0x7C),  # q
    114: (0x7C, 0x08, 0x04, 0x04, 0x08),  # r
    115: (0x48, 0x54, 0x54, 0x54, 0x20),  # s
    116: (0x04, 0x3F, 0x44, 0x40, 0x20),  # t
    117: (0x3C, 0x40, 0x40, 0x20, 0x7C),  # u
    118: (0x1C, 0x20, 0x40, 0x20, 0x1C),  # v
    119: (0x3C, 0x40, 0x30, 0x40, 0x3C),  # w
    120: (0x44, 0x28, 0x10, 0x28, 0x44),  # x
    121: (0x0C, 0x50, 0x50, 0x50, 0x3C),  # y
    122: (0x44, 0x64, 0x54, 0x4C, 0x44),  # z
    123: (0x00, 0x08, 0x36, 0x41, 0x00),  # {
    124: (0x00, 0x00, 0x7F, 0x00, 0x00),  # |
    125: (0x00, 0x41, 0x36, 0x08, 0x00),  # }
    126: (0x02, 0x01, 0x02, 0x04, 0x02),  # ~
    127: (0x06, 0x09, 0x09, 0x06, 0x00),  # Degree symbol (°)
}


class InlandEInkAdapter(DisplayPort, TelemetryPublisherPort):
    """Adapter for Inland / Micro Center SPI E-Ink displays."""

    def __init__(
        self,
        model: EInkModel = EInkModel.INLAND_2_13_INCH,
        min_refresh_interval_s: float = 10.0,
        heartbeat_interval_s: float = 30.0,
        full_refresh_interval: int = 25,
    ) -> None:
        self._model = model
        self._width, self._height = model.value
        self._row_bytes = (self._width + 7) // 8
        self._buffer_size = self._row_bytes * self._height
        self._buffer = bytearray([0xFF] * self._buffer_size)

        self._min_refresh_interval_s = min_refresh_interval_s
        self._heartbeat_interval_s = heartbeat_interval_s
        self._full_refresh_interval = full_refresh_interval

        self._last_view: DisplayView | None = None
        self._last_refresh_time_s = -1000.0
        self._refresh_count = 0
        self._is_sleeping = False
        self._is_initialized = False

    @property
    def width(self) -> int:
        return self._width

    @property
    def height(self) -> int:
        return self._height

    @property
    def refresh_count(self) -> int:
        return self._refresh_count

    @property
    def is_sleeping(self) -> bool:
        return self._is_sleeping

    def begin(self) -> None:
        """Initialize display buffer and status."""
        self._is_initialized = True
        self._is_sleeping = False
        self.clear_buffer(COLOR_WHITE)

    def clear(self) -> None:
        """Clear the frame buffer and flush blank to screen."""
        self.clear_buffer(COLOR_WHITE)
        self._refresh_count += 1

    def sleep(self) -> None:
        """Deep sleep mode for e-ink controller."""
        self._is_sleeping = True

    def is_busy(self) -> bool:
        """Check hardware busy flag."""
        return False

    def clear_buffer(self, color: int = COLOR_WHITE) -> None:
        """Fill frame buffer with 1 (white) or 0 (black)."""
        val = 0xFF if color == COLOR_WHITE else 0x00
        for i in range(self._buffer_size):
            self._buffer[i] = val

    def draw_pixel(self, x: int, y: int, color: int) -> None:
        """Plot a single pixel."""
        if x < 0 or x >= self._width or y < 0 or y >= self._height:
            return
        idx = (y * self._row_bytes) + (x // 8)
        bit = 0x80 >> (x % 8)
        if color == COLOR_WHITE:
            self._buffer[idx] |= bit
        else:
            self._buffer[idx] &= ~bit

    def get_pixel(self, x: int, y: int) -> int:
        """Sample pixel color: COLOR_WHITE (1) or COLOR_BLACK (0)."""
        if x < 0 or x >= self._width or y < 0 or y >= self._height:
            return COLOR_WHITE
        idx = (y * self._row_bytes) + (x // 8)
        bit = 0x80 >> (x % 8)
        return COLOR_WHITE if (self._buffer[idx] & bit) else COLOR_BLACK

    def draw_hline(self, x: int, y: int, w: int, color: int) -> None:
        """Draw horizontal line."""
        for i in range(w):
            self.draw_pixel(x + i, y, color)

    def draw_vline(self, x: int, y: int, h: int, color: int) -> None:
        """Draw vertical line."""
        for i in range(h):
            self.draw_pixel(x, y + i, color)

    def draw_rect(self, x: int, y: int, w: int, h: int, color: int) -> None:
        """Draw hollow rectangle."""
        self.draw_hline(x, y, w, color)
        self.draw_hline(x, y + h - 1, w, color)
        self.draw_vline(x, y, h, color)
        self.draw_vline(x + w - 1, y, h, color)

    def fill_rect(self, x: int, y: int, w: int, h: int, color: int) -> None:
        """Draw filled rectangle."""
        for row in range(y, y + h):
            self.draw_hline(x, row, w, color)

    def draw_char(self, x: int, y: int, char: str, scale: int = 1, color: int = COLOR_BLACK) -> None:
        """Render a single character using 5x7 font."""
        c = ord(char)
        if c == 0xB0 or c == 0xDF:
            c = 127  # degree sign
        if c not in FONT_5X7:
            c = 32

        glyph = FONT_5X7[c]
        for col_idx, col_byte in enumerate(glyph):
            for row_idx in range(7):
                if col_byte & (1 << row_idx):
                    if scale == 1:
                        self.draw_pixel(x + col_idx, y + row_idx, color)
                    else:
                        self.fill_rect(
                            x + (col_idx * scale),
                            y + (row_idx * scale),
                            scale,
                            scale,
                            color,
                        )

    def draw_string(self, x: int, y: int, text: str, scale: int = 1, color: int = COLOR_BLACK) -> None:
        """Draw a text string horizontally."""
        char_w = 6 * scale
        for i, ch in enumerate(text):
            self.draw_char(x + (i * char_w), y, ch, scale, color)

    def draw_progress_bar(
        self,
        x: int,
        y: int,
        w: int,
        h: int,
        pct: float,
        label: str = "",
    ) -> None:
        """Render a graphical progress bar."""
        clamped_pct = max(0.0, min(100.0, pct))
        self.draw_rect(x, y, w, h, COLOR_BLACK)
        inner_w = int((w - 4) * (clamped_pct / 100.0))
        if inner_w > 0 and h > 4:
            self.fill_rect(x + 2, y + 2, inner_w, h - 4, COLOR_BLACK)
        if label:
            self.draw_string(x, y - 8, label, 1, COLOR_BLACK)

    def render(self, view: DisplayView, force_full: bool = False) -> None:
        """Render full layout to buffer and simulate e-ink refresh."""
        self.clear_buffer(COLOR_WHITE)

        # 1. Header Bar (0..14)
        self.draw_string(4, 3, "ESP32 SMOKER CONTROL", 1, COLOR_BLACK)
        if view.lid_open:
            self.fill_rect(170, 1, 76, 12, COLOR_BLACK)
            self.draw_string(174, 3, "LID OPEN", 1, COLOR_WHITE)
        else:
            self.draw_string(170, 3, view.format_setpoint(), 1, COLOR_BLACK)
        self.draw_hline(0, 15, self._width, COLOR_BLACK)

        # 2. Pit Section (Large 3x digits)
        self.draw_string(6, 20, "PIT", 1, COLOR_BLACK)
        if not view.pit_valid:
            self.draw_string(6, 30, "SENSOR FAULT", 2, COLOR_BLACK)
        else:
            pit_str = f"{view.pit_temp_f:.1f}"
            self.draw_string(6, 29, pit_str, 3, COLOR_BLACK)
            deg_x = 6 + len(pit_str) * 18 + 4
            self.draw_char(deg_x, 28, "\x7f", 2, COLOR_BLACK)
            self.draw_char(deg_x + 14, 29, "F", 2, COLOR_BLACK)

        # 3. Meat Section (2x digits)
        self.draw_string(145, 20, "MEAT", 1, COLOR_BLACK)
        if view.is_meat_wireless and view.meat_battery_pct is not None and view.meat_battery_pct >= 0:
            self.draw_string(180, 20, f"[{view.meat_battery_pct}%]", 1, COLOR_BLACK)

        if not view.meat_valid:
            self.draw_string(145, 34, "--.- F", 2, COLOR_BLACK)
        else:
            meat_str = f"{view.meat_temp_f:.1f}"
            self.draw_string(145, 33, meat_str, 2, COLOR_BLACK)
            m_deg_x = 145 + len(meat_str) * 12 + 2
            self.draw_char(m_deg_x, 32, "\x7f", 1, COLOR_BLACK)
            self.draw_char(m_deg_x + 8, 33, "F", 2, COLOR_BLACK)

        if view.is_meat_wireless and view.meat_probe_name:
            self.draw_string(145, 52, view.meat_probe_name[:16], 1, COLOR_BLACK)

        self.draw_hline(0, 64, self._width, COLOR_BLACK)

        # 4. Actuators & Status Section (66..121)
        self.draw_string(6, 69, f"FAN {view.blower_pct:3.0f}%", 1, COLOR_BLACK)
        self.draw_progress_bar(64, 68, 60, 9, view.blower_pct)

        self.draw_string(132, 69, f"DMP {view.damper_pct:3.0f}%", 1, COLOR_BLACK)
        self.draw_progress_bar(186, 68, 56, 9, view.damper_pct)

        self.draw_hline(0, 84, self._width, COLOR_BLACK)

        status_text = view.status if not view.lid_open else "LID PAUSE ACTIVE"
        self.draw_string(6, 90, f"STATUS: {status_text}", 1, COLOR_BLACK)

        probe_src = (
            f"BLE: {view.meat_probe_name}" if view.is_meat_wireless else "WIRED K-TYPE"
        )
        self.draw_string(6, 104, f"SOURCE: {probe_src}", 1, COLOR_BLACK)

        self._refresh_count += 1
        self._last_view = view

    def update(self, now_s: float, view: DisplayView) -> bool:
        """Smart refresh update scheduler. Returns True if display refreshed."""
        if not self._is_initialized:
            self.begin()

        if self._refresh_count > 0 and (now_s - self._last_refresh_time_s) < self._min_refresh_interval_s:
            return False

        changed = view.has_significant_change(self._last_view)
        heartbeat = (now_s - self._last_refresh_time_s) >= self._heartbeat_interval_s

        if changed or heartbeat or self._refresh_count == 0:
            force_full = (self._refresh_count % self._full_refresh_interval == 0)
            self.render(view, force_full)
            self._last_refresh_time_s = now_s
            return True

        return False

    def publish(self, snapshot: TelemetrySnapshot) -> None:
        """TelemetryPublisherPort implementation: auto-update from telemetry."""
        view = DisplayView.from_telemetry(snapshot)
        self.update(snapshot.timestamp_s, view)

    def dump_ascii(self, step: int = 4) -> str:
        """Desktop ASCII art dumper for unit testing and visualization."""
        lines: list[str] = []
        for y in range(0, self._height, step):
            row_chars = []
            for x in range(0, self._width, step):
                pixel = self.get_pixel(x, y)
                row_chars.append("#" if pixel == COLOR_BLACK else ".")
            lines.append("".join(row_chars))
        return "\n".join(lines)
