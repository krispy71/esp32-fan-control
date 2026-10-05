#pragma once

#include <cstdint>

namespace SmokerController::Domain {

// Value snapshot: copying it also copies strings, so it can cross task boundaries.
struct TelemetrySnapshot {
    uint32_t timestamp_ms{0};
    float pit_temp_f{0.0f};
    float meat_temp_f{0.0f};
    float setpoint_f{225.0f};
    float damper_position_pct{0.0f};
    float blower_speed_pct{0.0f};
    float demand_pct{0.0f};
    bool is_pit_valid{false};
    bool is_meat_valid{false};
    bool lid_open{false};
    char status[32]{"INITIALIZING"};
    bool is_meat_wireless{false};
    int8_t meat_battery_pct{-1};
    char meat_probe_name[32]{""};
};

} // namespace SmokerController::Domain
