#pragma once

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include "../../Domain/DisplayView.hpp"
#include "../../Services/Ports/DisplayPort.hpp"
#include "../../Services/Ports/TelemetryPort.hpp"

#ifdef ARDUINO
#include <Arduino.h>
#include <SPI.h>
#endif

namespace SmokerController::Adapters::Display {

enum class EInkModel {
    Inland_2_13_Inch, // 250 x 122 pixels (landscape)
    Inland_1_54_Inch  // 200 x 200 pixels
};

/**
 * Standard 5x7 ASCII font table with degree sign (ASCII 0xDF / 0xB0 mapped to 0x7E/0x7F).
 * Column-major format (5 bytes per character).
 */
static const uint8_t FONT_5X7[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // 32: Space
    {0x00, 0x00, 0x5F, 0x00, 0x00}, // 33: !
    {0x00, 0x07, 0x00, 0x07, 0x00}, // 34: "
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, // 35: #
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // 36: $
    {0x23, 0x13, 0x08, 0x64, 0x62}, // 37: %
    {0x36, 0x49, 0x55, 0x22, 0x50}, // 38: &
    {0x00, 0x05, 0x03, 0x00, 0x00}, // 39: '
    {0x00, 0x1C, 0x22, 0x41, 0x00}, // 40: (
    {0x00, 0x41, 0x22, 0x1C, 0x00}, // 41: )
    {0x14, 0x08, 0x3E, 0x08, 0x14}, // 42: *
    {0x08, 0x08, 0x3E, 0x08, 0x08}, // 43: +
    {0x00, 0x50, 0x30, 0x00, 0x00}, // 44: ,
    {0x08, 0x08, 0x08, 0x08, 0x08}, // 45: -
    {0x00, 0x60, 0x60, 0x00, 0x00}, // 46: .
    {0x20, 0x10, 0x08, 0x04, 0x02}, // 47: /
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 48: 0
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 49: 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 50: 2
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 51: 3
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 52: 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 53: 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 54: 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 55: 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 56: 8
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // 57: 9
    {0x00, 0x36, 0x36, 0x00, 0x00}, // 58: :
    {0x00, 0x56, 0x36, 0x00, 0x00}, // 59: ;
    {0x08, 0x14, 0x22, 0x41, 0x00}, // 60: <
    {0x14, 0x14, 0x14, 0x14, 0x14}, // 61: =
    {0x00, 0x41, 0x22, 0x14, 0x08}, // 62: >
    {0x02, 0x01, 0x51, 0x09, 0x06}, // 63: ?
    {0x32, 0x49, 0x79, 0x41, 0x3E}, // 64: @
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // 65: A
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // 66: B
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // 67: C
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // 68: D
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // 69: E
    {0x7F, 0x09, 0x09, 0x09, 0x01}, // 70: F
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, // 71: G
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // 72: H
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // 73: I
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // 74: J
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // 75: K
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // 76: L
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // 77: M
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // 78: N
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // 79: O
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // 80: P
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // 81: Q
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // 82: R
    {0x46, 0x49, 0x49, 0x49, 0x31}, // 83: S
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // 84: T
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // 85: U
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // 86: V
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, // 87: W
    {0x63, 0x14, 0x08, 0x14, 0x63}, // 88: X
    {0x07, 0x08, 0x70, 0x08, 0x07}, // 89: Y
    {0x61, 0x51, 0x49, 0x45, 0x43}, // 90: Z
    {0x00, 0x7F, 0x41, 0x41, 0x00}, // 91: [
    {0x02, 0x04, 0x08, 0x10, 0x20}, // 92: Backslash
    {0x00, 0x41, 0x41, 0x7F, 0x00}, // 93: ]
    {0x04, 0x02, 0x01, 0x02, 0x04}, // 94: ^
    {0x40, 0x40, 0x40, 0x40, 0x40}, // 95: _
    {0x00, 0x01, 0x02, 0x04, 0x00}, // 96: `
    {0x20, 0x54, 0x54, 0x54, 0x78}, // 97: a
    {0x7F, 0x48, 0x44, 0x44, 0x38}, // 98: b
    {0x38, 0x44, 0x44, 0x44, 0x20}, // 99: c
    {0x38, 0x44, 0x44, 0x48, 0x7F}, // 100: d
    {0x38, 0x54, 0x54, 0x54, 0x18}, // 101: e
    {0x08, 0x7E, 0x09, 0x01, 0x02}, // 102: f
    {0x0C, 0x52, 0x52, 0x52, 0x3E}, // 103: g
    {0x7F, 0x08, 0x04, 0x04, 0x78}, // 104: h
    {0x00, 0x44, 0x7D, 0x40, 0x00}, // 105: i
    {0x20, 0x40, 0x44, 0x3D, 0x00}, // 106: j
    {0x7F, 0x10, 0x28, 0x44, 0x00}, // 107: k
    {0x00, 0x41, 0x7F, 0x40, 0x00}, // 108: l
    {0x7C, 0x04, 0x18, 0x04, 0x78}, // 109: m
    {0x7C, 0x08, 0x04, 0x04, 0x78}, // 110: n
    {0x38, 0x44, 0x44, 0x44, 0x38}, // 111: o
    {0x7C, 0x14, 0x14, 0x14, 0x08}, // 112: p
    {0x08, 0x14, 0x14, 0x18, 0x7C}, // 113: q
    {0x7C, 0x08, 0x04, 0x04, 0x08}, // 114: r
    {0x48, 0x54, 0x54, 0x54, 0x20}, // 115: s
    {0x04, 0x3F, 0x44, 0x40, 0x20}, // 116: t
    {0x3C, 0x40, 0x40, 0x20, 0x7C}, // 117: u
    {0x1C, 0x20, 0x40, 0x20, 0x1C}, // 118: v
    {0x3C, 0x40, 0x30, 0x40, 0x3C}, // 119: w
    {0x44, 0x28, 0x10, 0x28, 0x44}, // 120: x
    {0x0C, 0x50, 0x50, 0x50, 0x3C}, // 121: y
    {0x44, 0x64, 0x54, 0x4C, 0x44}, // 122: z
    {0x00, 0x08, 0x36, 0x41, 0x00}, // 123: {
    {0x00, 0x00, 0x7F, 0x00, 0x00}, // 124: |
    {0x00, 0x41, 0x36, 0x08, 0x00}, // 125: }
    {0x02, 0x01, 0x02, 0x04, 0x02}, // 126: ~
    {0x06, 0x09, 0x09, 0x06, 0x00}  // 127: Degree symbol (°)
};

/**
 * Concrete hardware adapter for Inland (Micro Center) E-Ink / E-Paper displays.
 * Interfaces via 4-wire SPI (CS, DC, RST, BUSY, SCK, MOSI).
 * Implements both IDisplayPort and ITelemetryPublisherPort.
 */
class InlandEInkAdapter : public Services::Ports::IDisplayPort,
                          public Services::Ports::ITelemetryPublisherPort {
public:
    static constexpr uint8_t COLOR_BLACK = 0;
    static constexpr uint8_t COLOR_WHITE = 1;

    InlandEInkAdapter(
        uint8_t cs_pin = 4,
        uint8_t dc_pin = 22,
        uint8_t rst_pin = 16,
        uint8_t busy_pin = 17,
        uint8_t sck_pin = 18,
        uint8_t mosi_pin = 23,
        EInkModel model = EInkModel::Inland_2_13_Inch
    ) noexcept
        : cs_pin_(cs_pin),
          dc_pin_(dc_pin),
          rst_pin_(rst_pin),
          busy_pin_(busy_pin),
          sck_pin_(sck_pin),
          mosi_pin_(mosi_pin),
          model_(model),
          width_(model == EInkModel::Inland_2_13_Inch ? 250 : 200),
          height_(model == EInkModel::Inland_2_13_Inch ? 122 : 200),
          row_bytes_((width_ + 7) / 8),
          buffer_size_(row_bytes_ * height_)
    {
        buffer_ = new uint8_t[buffer_size_];
        std::memset(buffer_, 0xFF, buffer_size_); // Default all white
    }

    ~InlandEInkAdapter() override {
        delete[] buffer_;
    }

    void begin() noexcept override {
#ifdef ARDUINO
        pinMode(cs_pin_, OUTPUT);
        pinMode(dc_pin_, OUTPUT);
        pinMode(rst_pin_, OUTPUT);
        pinMode(busy_pin_, INPUT);

        digitalWrite(cs_pin_, HIGH);
        digitalWrite(dc_pin_, HIGH);

        // Hardware reset
        digitalWrite(rst_pin_, LOW);
        delay(20);
        digitalWrite(rst_pin_, HIGH);
        delay(20);

        waitBusy(2000);

        // Initialize display controller (SSD1680 standard)
        sendCommand(0x12); // Software Reset
        waitBusy(2000);

        sendCommand(0x01); // Driver output control
        sendData((height_ - 1) & 0xFF);
        sendData(((height_ - 1) >> 8) & 0xFF);
        sendData(0x00);

        sendCommand(0x11); // Data entry mode setting (X increment, Y increment)
        sendData(0x03);

        setRamWindow(0, width_ - 1, 0, height_ - 1);
#endif
        is_initialized_ = true;
        clearBuffer(COLOR_WHITE);
    }

    void clear() noexcept override {
        clearBuffer(COLOR_WHITE);
        pushBuffer(true);
    }

    void sleep() noexcept override {
#ifdef ARDUINO
        sendCommand(0x10); // Deep sleep mode
        sendData(0x01);
#endif
    }

    [[nodiscard]] bool isBusy() const noexcept override {
#ifdef ARDUINO
        return digitalRead(busy_pin_) == HIGH;
#else
        return false;
#endif
    }

    void clearBuffer(uint8_t color) noexcept {
        uint8_t val = (color == COLOR_WHITE) ? 0xFF : 0x00;
        std::memset(buffer_, val, buffer_size_);
    }

    void drawPixel(int16_t x, int16_t y, uint8_t color) noexcept {
        if (x < 0 || x >= width_ || y < 0 || y >= height_) return;
        uint32_t idx = (y * row_bytes_) + (x / 8);
        uint8_t bit = 0x80 >> (x % 8);
        if (color == COLOR_WHITE) {
            buffer_[idx] |= bit;
        } else {
            buffer_[idx] &= ~bit;
        }
    }

    void drawHLine(int16_t x, int16_t y, int16_t w, uint8_t color) noexcept {
        for (int16_t i = 0; i < w; ++i) {
            drawPixel(x + i, y, color);
        }
    }

    void drawVLine(int16_t x, int16_t y, int16_t h, uint8_t color) noexcept {
        for (int16_t i = 0; i < h; ++i) {
            drawPixel(x, y + i, color);
        }
    }

    void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color) noexcept {
        drawHLine(x, y, w, color);
        drawHLine(x, y + h - 1, w, color);
        drawVLine(x, y, h, color);
        drawVLine(x + w - 1, y, h, color);
    }

    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color) noexcept {
        for (int16_t i = 0; i < h; ++i) {
            drawHLine(x, y + i, w, color);
        }
    }

    void drawChar(int16_t x, int16_t y, char c, uint8_t scale = 1, uint8_t color = COLOR_BLACK) noexcept {
        uint8_t idx = 0;
        if (c >= 32 && c <= 126) {
            idx = static_cast<uint8_t>(c - 32);
        } else if (static_cast<uint8_t>(c) == 0xB0 || static_cast<uint8_t>(c) == 0xDF) {
            idx = 127 - 32; // Degree symbol
        } else {
            idx = 0; // Space
        }

        const uint8_t* font_col = FONT_5X7[idx];
        for (uint8_t col = 0; col < 5; ++col) {
            uint8_t line = font_col[col];
            for (uint8_t row = 0; row < 7; ++row) {
                if (line & (1 << row)) {
                    if (scale == 1) {
                        drawPixel(x + col, y + row, color);
                    } else {
                        fillRect(x + col * scale, y + row * scale, scale, scale, color);
                    }
                }
            }
        }
    }

    void drawString(int16_t x, int16_t y, const char* str, uint8_t scale = 1, uint8_t color = COLOR_BLACK) noexcept {
        if (!str) return;
        int16_t cur_x = x;
        while (*str) {
            drawChar(cur_x, y, *str, scale, color);
            cur_x += 6 * scale;
            ++str;
        }
    }

    void drawProgressBar(int16_t x, int16_t y, int16_t w, int16_t h, float pct, const char* label) noexcept {
        drawRect(x, y, w, h, COLOR_BLACK);
        float clamped = std::clamp(pct, 0.0f, 100.0f);
        int16_t fill_w = static_cast<int16_t>((clamped / 100.0f) * (w - 4));
        if (fill_w > 0) {
            fillRect(x + 2, y + 2, fill_w, h - 4, COLOR_BLACK);
        }
        if (label && label[0] != '\0') {
            drawString(x + w + 4, y + (h - 7) / 2, label, 1, COLOR_BLACK);
        }
    }

    /**
     * Renders full technical telemetry dashboard formatted for e-ink readability.
     */
    void render(const Domain::DisplayView& view, bool force_full_refresh = false) noexcept override {
        clearBuffer(COLOR_WHITE);

        if (model_ == EInkModel::Inland_2_13_Inch) {
            renderLandscape2_13(view);
        } else {
            renderSquare1_54(view);
        }

        pushBuffer(force_full_refresh);
        last_view_ = view;
        if (view.timestamp_ms > 0) {
            last_refresh_ms_ = view.timestamp_ms;
        }
        ++refresh_count_;
    }

    /**
     * Periodic update called by loop() on Core 0.
     * Enforces e-ink update cooldown (min 10s) and triggers refresh on change or heartbeat (30s).
     */
    void update(uint32_t now_ms, const Domain::DisplayView& view) noexcept {
        if (!is_initialized_) return;

        bool cooldown_met = (now_ms >= last_refresh_ms_) && ((now_ms - last_refresh_ms_) >= MIN_REFRESH_INTERVAL_MS);
        if (!cooldown_met) return;

        bool changed = view.hasSignificantChange(last_view_);
        bool heartbeat = (now_ms >= last_refresh_ms_) && ((now_ms - last_refresh_ms_) >= HEARTBEAT_REFRESH_INTERVAL_MS);

        if (changed || heartbeat || refresh_count_ == 0) {
            // Periodic full refresh every 25 updates to wipe ghosting
            bool force_full = (refresh_count_ % 25 == 0);
            render(view, force_full);
            last_refresh_ms_ = now_ms;
        }
    }

    // TelemetryPublisherPort implementation
    void publish(const Services::Ports::TelemetrySnapshot& snapshot) noexcept override {
        auto view = Domain::DisplayView::fromTelemetry(snapshot);
        update(snapshot.timestamp_ms, view);
    }

    [[nodiscard]] const uint8_t* buffer() const noexcept { return buffer_; }
    [[nodiscard]] uint32_t bufferSize() const noexcept { return buffer_size_; }
    [[nodiscard]] uint16_t width() const noexcept { return width_; }
    [[nodiscard]] uint16_t height() const noexcept { return height_; }
    [[nodiscard]] uint32_t refreshCount() const noexcept { return refresh_count_; }

    /**
     * Desktop ASCII art dumper for unit tests and verification.
     */
    void dumpAscii(char* out, size_t max_len, uint8_t step = 4) const noexcept {
        if (!out || max_len == 0) return;
        size_t pos = 0;
        for (int16_t y = 0; y < height_; y += step) {
            for (int16_t x = 0; x < width_; x += step) {
                if (pos + 2 >= max_len) break;
                uint8_t pixel = getPixel(x, y);
                out[pos++] = (pixel == COLOR_BLACK) ? '#' : '.';
            }
            if (pos + 2 < max_len) {
                out[pos++] = '\n';
            }
        }
        out[pos] = '\0';
    }

    [[nodiscard]] uint8_t getPixel(int16_t x, int16_t y) const noexcept {
        if (x < 0 || x >= width_ || y < 0 || y >= height_) return COLOR_WHITE;
        uint32_t idx = (y * row_bytes_) + (x / 8);
        uint8_t bit = 0x80 >> (x % 8);
        return (buffer_[idx] & bit) ? COLOR_WHITE : COLOR_BLACK;
    }

private:
    void renderLandscape2_13(const Domain::DisplayView& v) noexcept {
        // 1. Header Bar (0 to 14)
        drawString(4, 3, "ESP32 SMOKER CONTROL", 1, COLOR_BLACK);
        if (v.lid_open) {
            fillRect(170, 1, 76, 12, COLOR_BLACK);
            drawString(174, 3, "LID OPEN", 1, COLOR_WHITE);
        } else {
            char set_buf[20];
            v.formatSetpoint(set_buf, sizeof(set_buf));
            drawString(170, 3, set_buf, 1, COLOR_BLACK);
        }
        drawHLine(0, 15, width_, COLOR_BLACK);

        // 2. Pit Temperature Section (Large 3x digits)
        drawString(6, 20, "PIT", 1, COLOR_BLACK);
        char pit_str[24];
        if (!v.pit_valid) {
            drawString(6, 30, "SENSOR FAULT", 2, COLOR_BLACK);
        } else {
            std::snprintf(pit_str, sizeof(pit_str), "%.1f", v.pit_temp_f);
            drawString(6, 29, pit_str, 3, COLOR_BLACK);
            int16_t deg_x = 6 + static_cast<int16_t>(std::strlen(pit_str)) * 18 + 4;
            drawChar(deg_x, 28, '\x7F', 2, COLOR_BLACK); // Degree
            drawChar(deg_x + 14, 29, 'F', 2, COLOR_BLACK);
        }

        // 3. Meat Temperature Section (2x digits)
        drawString(145, 20, "MEAT", 1, COLOR_BLACK);
        if (v.is_meat_wireless && v.meat_battery_pct >= 0) {
            char batt_buf[16];
            std::snprintf(batt_buf, sizeof(batt_buf), "[%d%%]", v.meat_battery_pct);
            drawString(180, 20, batt_buf, 1, COLOR_BLACK);
        }
        char meat_str[24];
        if (!v.meat_valid) {
            drawString(145, 34, "--.- F", 2, COLOR_BLACK);
        } else {
            std::snprintf(meat_str, sizeof(meat_str), "%.1f", v.meat_temp_f);
            drawString(145, 33, meat_str, 2, COLOR_BLACK);
            int16_t m_deg_x = 145 + static_cast<int16_t>(std::strlen(meat_str)) * 12 + 2;
            drawChar(m_deg_x, 32, '\x7F', 1, COLOR_BLACK);
            drawChar(m_deg_x + 8, 33, 'F', 2, COLOR_BLACK);
        }
        if (v.is_meat_wireless && v.meat_probe_name[0] != '\0') {
            drawString(145, 52, v.meat_probe_name, 1, COLOR_BLACK);
        }

        drawHLine(0, 64, width_, COLOR_BLACK);

        // 4. Actuators & Status Section (66 to 121)
        char fan_lbl[16];
        std::snprintf(fan_lbl, sizeof(fan_lbl), "FAN %3.0f%%", v.blower_pct);
        drawString(6, 69, fan_lbl, 1, COLOR_BLACK);
        drawProgressBar(64, 68, 60, 9, v.blower_pct, "");

        char dmp_lbl[16];
        std::snprintf(dmp_lbl, sizeof(dmp_lbl), "DMP %3.0f%%", v.damper_pct);
        drawString(132, 69, dmp_lbl, 1, COLOR_BLACK);
        drawProgressBar(186, 68, 56, 9, v.damper_pct, "");

        drawHLine(0, 84, width_, COLOR_BLACK);

        // Status banner
        drawString(6, 90, "STATUS:", 1, COLOR_BLACK);
        drawString(54, 90, v.status, 1, COLOR_BLACK);

        char flow_str[32];
        std::snprintf(flow_str, sizeof(flow_str), "DEMAND: %3.0f%%", v.demand_pct);
        drawString(6, 105, flow_str, 1, COLOR_BLACK);

        // Border frame
        drawRect(0, 0, width_, height_, COLOR_BLACK);
    }

    void renderSquare1_54(const Domain::DisplayView& v) noexcept {
        drawString(10, 8, "SMOKER CONTROLLER", 1, COLOR_BLACK);
        drawHLine(0, 20, width_, COLOR_BLACK);

        // Pit Temp
        drawString(10, 26, "PIT TEMP", 1, COLOR_BLACK);
        char pit_str[24];
        if (!v.pit_valid) {
            drawString(10, 38, "ERR", 3, COLOR_BLACK);
        } else {
            std::snprintf(pit_str, sizeof(pit_str), "%.1f F", v.pit_temp_f);
            drawString(10, 38, pit_str, 3, COLOR_BLACK);
        }

        // Meat Temp
        drawString(10, 72, "MEAT TEMP", 1, COLOR_BLACK);
        char meat_str[24];
        if (!v.meat_valid) {
            drawString(10, 84, "--.- F", 2, COLOR_BLACK);
        } else {
            std::snprintf(meat_str, sizeof(meat_str), "%.1f F", v.meat_temp_f);
            drawString(10, 84, meat_str, 2, COLOR_BLACK);
        }

        drawHLine(0, 110, width_, COLOR_BLACK);

        // Actuator Bars
        char fan_lbl[16];
        std::snprintf(fan_lbl, sizeof(fan_lbl), "FAN %3.0f%%", v.blower_pct);
        drawString(10, 118, fan_lbl, 1, COLOR_BLACK);
        drawProgressBar(70, 117, 100, 10, v.blower_pct, "");

        char dmp_lbl[16];
        std::snprintf(dmp_lbl, sizeof(dmp_lbl), "DMP %3.0f%%", v.damper_pct);
        drawString(10, 136, dmp_lbl, 1, COLOR_BLACK);
        drawProgressBar(70, 135, 100, 10, v.damper_pct, "");

        drawHLine(0, 155, width_, COLOR_BLACK);

        drawString(10, 164, "STATUS:", 1, COLOR_BLACK);
        drawString(60, 164, v.status, 1, COLOR_BLACK);

        char set_buf[20];
        v.formatSetpoint(set_buf, sizeof(set_buf));
        drawString(10, 180, set_buf, 1, COLOR_BLACK);

        drawRect(0, 0, width_, height_, COLOR_BLACK);
    }

    void pushBuffer(bool full_refresh) noexcept {
#ifdef ARDUINO
        setRamWindow(0, width_ - 1, 0, height_ - 1);
        setRamAddress(0, 0);

        // Write Black/White RAM (0x24)
        sendCommand(0x24);
        SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
        digitalWrite(dc_pin_, HIGH);
        digitalWrite(cs_pin_, LOW);
        for (uint32_t i = 0; i < buffer_size_; ++i) {
            SPI.transfer(buffer_[i]);
        }
        digitalWrite(cs_pin_, HIGH);
        SPI.endTransaction();

        // Display Update sequence
        sendCommand(0x22);
        sendData(full_refresh ? 0xF7 : 0xC7); // Full vs fast partial refresh

        sendCommand(0x20); // Master Activation
        waitBusy(2500);

        sleep();
#endif
    }

#ifdef ARDUINO
    void sendCommand(uint8_t cmd) noexcept {
        digitalWrite(dc_pin_, LOW); // Command
        digitalWrite(cs_pin_, LOW);
        SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
        SPI.transfer(cmd);
        SPI.endTransaction();
        digitalWrite(cs_pin_, HIGH);
    }

    void sendData(uint8_t data) noexcept {
        digitalWrite(dc_pin_, HIGH); // Data
        digitalWrite(cs_pin_, LOW);
        SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
        SPI.transfer(data);
        SPI.endTransaction();
        digitalWrite(cs_pin_, HIGH);
    }

    void setRamWindow(uint16_t x_start, uint16_t x_end, uint16_t y_start, uint16_t y_end) noexcept {
        sendCommand(0x44); // Set RAM X start/end
        sendData((x_start / 8) & 0xFF);
        sendData((x_end / 8) & 0xFF);

        sendCommand(0x45); // Set RAM Y start/end
        sendData(y_start & 0xFF);
        sendData((y_start >> 8) & 0xFF);
        sendData(y_end & 0xFF);
        sendData((y_end >> 8) & 0xFF);
    }

    void setRamAddress(uint16_t x, uint16_t y) noexcept {
        sendCommand(0x4E); // Set RAM X address counter
        sendData((x / 8) & 0xFF);

        sendCommand(0x4F); // Set RAM Y address counter
        sendData(y & 0xFF);
        sendData((y >> 8) & 0xFF);
    }

    void waitBusy(uint32_t timeout_ms = 3000) noexcept {
        uint32_t start = millis();
        while (digitalRead(busy_pin_) == HIGH) {
            if (millis() - start >= timeout_ms) break;
            delay(10);
        }
    }
#endif

    uint8_t cs_pin_;
    uint8_t dc_pin_;
    uint8_t rst_pin_;
    uint8_t busy_pin_;
    uint8_t sck_pin_;
    uint8_t mosi_pin_;
    EInkModel model_;
    int16_t width_;
    int16_t height_;
    uint16_t row_bytes_;
    uint32_t buffer_size_;
    uint8_t* buffer_{nullptr};

    bool is_initialized_{false};
    uint32_t last_refresh_ms_{0};
    uint32_t refresh_count_{0};
    Domain::DisplayView last_view_{};

    static constexpr uint32_t MIN_REFRESH_INTERVAL_MS = 10000;      // 10s cooldown
    static constexpr uint32_t HEARTBEAT_REFRESH_INTERVAL_MS = 30000; // 30s heartbeat
};

} // namespace SmokerController::Adapters::Display
