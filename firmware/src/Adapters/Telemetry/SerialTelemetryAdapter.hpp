#pragma once

#include "../../Services/Ports/TelemetryPort.hpp"

#ifdef ARDUINO
#include <Arduino.h>
#else
#include <iostream>
#endif

namespace SmokerController::Adapters::Telemetry {

class SerialTelemetryAdapter : public Services::Ports::ITelemetryPublisherPort {
public:
    void publish(const Services::Ports::TelemetrySnapshot& s) noexcept override {
#ifdef ARDUINO
        char meat_str[48];
        if (s.is_meat_wireless && s.is_meat_valid) {
            snprintf(meat_str, sizeof(meat_str), "%.1f F [%s %d%%]", s.meat_temp_f, s.meat_probe_name ? s.meat_probe_name : "BLE", static_cast<int>(s.meat_battery_pct));
        } else if (s.is_meat_valid) {
            snprintf(meat_str, sizeof(meat_str), "%.1f F [Wired]", s.meat_temp_f);
        } else {
            snprintf(meat_str, sizeof(meat_str), "N/C");
        }

        Serial.printf(
            "[%lu ms] Pit: %.1f F | Set: %.1f F | Meat: %s | Demand: %.1f%% | Damper: %.1f%% | Fan: %.1f%% | Lid: %s | State: %s\n",
            (unsigned long)s.timestamp_ms,
            s.pit_temp_f,
            s.setpoint_f,
            meat_str,
            s.demand_pct,
            s.damper_position_pct,
            s.blower_speed_pct,
            s.lid_open ? "OPEN" : "CLOSED",
            s.status
        );
#else
        std::cout << "[" << s.timestamp_ms << " ms] Pit: " << s.pit_temp_f
                  << " F | Set: " << s.setpoint_f
                  << " F | Demand: " << s.demand_pct
                  << "% | Damper: " << s.damper_position_pct
                  << "% | Fan: " << s.blower_speed_pct
                  << "% | State: " << s.status << "\n";
#endif
    }
};

} // namespace SmokerController::Adapters::Telemetry
