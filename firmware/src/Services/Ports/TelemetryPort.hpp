#pragma once

#include <cstdint>

namespace SmokerController::Services::Ports {

struct TelemetrySnapshot {
    uint32_t timestamp_ms;
    float pit_temp_f;
    float meat_temp_f;
    float setpoint_f;
    float damper_position_pct;
    float blower_speed_pct;
    float demand_pct;
    bool is_pit_valid;
    bool is_meat_valid;
    bool lid_open;
    const char* status;
    bool is_meat_wireless{false};
    int8_t meat_battery_pct{-1};
    const char* meat_probe_name{""};
};

class ITelemetryPublisherPort {
public:
    virtual ~ITelemetryPublisherPort() = default;
    virtual void publish(const TelemetrySnapshot& snapshot) = 0;
};

} // namespace SmokerController::Services::Ports
