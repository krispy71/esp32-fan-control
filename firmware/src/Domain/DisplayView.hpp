#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include "Temperature.hpp"
#include "Telemetry.hpp"

namespace SmokerController::Domain {

/**
 * Pure domain representation of the status and temperature telemetry
 * formatted for visual display on local screens (E-Ink, OLED, LCD).
 */
struct DisplayView {
    float pit_temp_f{0.0f};
    bool pit_valid{false};
    float meat_temp_f{0.0f};
    bool meat_valid{false};
    float setpoint_f{225.0f};
    float damper_pct{0.0f};
    float blower_pct{0.0f};
    float demand_pct{0.0f};
    bool lid_open{false};
    char status[32]{"INITIALIZING"};
    bool is_meat_wireless{false};
    int8_t meat_battery_pct{-1};
    char meat_probe_name[32]{""};
    uint32_t timestamp_ms{0};

    [[nodiscard]] static DisplayView fromTelemetry(const TelemetrySnapshot& s) noexcept {
        DisplayView v{};
        v.pit_temp_f = s.pit_temp_f;
        v.pit_valid = s.is_pit_valid;
        v.meat_temp_f = s.meat_temp_f;
        v.meat_valid = s.is_meat_valid;
        v.setpoint_f = s.setpoint_f;
        v.damper_pct = s.damper_position_pct;
        v.blower_pct = s.blower_speed_pct;
        v.demand_pct = s.demand_pct;
        v.lid_open = s.lid_open;
        v.is_meat_wireless = s.is_meat_wireless;
        v.meat_battery_pct = s.meat_battery_pct;
        v.timestamp_ms = s.timestamp_ms;

            std::strncpy(v.status, s.status, sizeof(v.status) - 1);
            v.status[sizeof(v.status) - 1] = '\0';
            std::strncpy(v.meat_probe_name, s.meat_probe_name, sizeof(v.meat_probe_name) - 1);
            v.meat_probe_name[sizeof(v.meat_probe_name) - 1] = '\0';
        return v;
    }

    /**
     * Determines if telemetry has changed significantly enough to warrant
     * an E-Ink physical refresh (avoids unnecessary display wear and flashing).
     */
    [[nodiscard]] bool hasSignificantChange(
        const DisplayView& prev,
        float temp_thresh = 0.5f,
        float output_thresh = 5.0f
    ) const noexcept {
        if (pit_valid != prev.pit_valid || meat_valid != prev.meat_valid || lid_open != prev.lid_open) {
            return true;
        }
        if (std::strcmp(status, prev.status) != 0) {
            return true;
        }
        if (std::abs(setpoint_f - prev.setpoint_f) >= 0.5f) {
            return true;
        }
        if (pit_valid && std::abs(pit_temp_f - prev.pit_temp_f) >= temp_thresh) {
            return true;
        }
        if (meat_valid && std::abs(meat_temp_f - prev.meat_temp_f) >= temp_thresh) {
            return true;
        }
        if (std::abs(damper_pct - prev.damper_pct) >= output_thresh ||
            std::abs(blower_pct - prev.blower_pct) >= output_thresh) {
            return true;
        }
        return false;
    }

    void formatPit(char* out, size_t max_len) const noexcept {
        if (!pit_valid) {
            std::snprintf(out, max_len, "ERR");
        } else {
            std::snprintf(out, max_len, "%.1f F", pit_temp_f);
        }
    }

    void formatMeat(char* out, size_t max_len) const noexcept {
        if (!meat_valid) {
            std::snprintf(out, max_len, "--.- F");
        } else {
            std::snprintf(out, max_len, "%.1f F", meat_temp_f);
        }
    }

    void formatSetpoint(char* out, size_t max_len) const noexcept {
        std::snprintf(out, max_len, "Set: %.0f F", setpoint_f);
    }
};

} // namespace SmokerController::Domain
