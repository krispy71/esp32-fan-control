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
        Serial.printf(
            "[%lu ms] Pit: %.1f F | Set: %.1f F | Meat: %.1f F | Demand: %.1f%% | Damper: %.1f%% | Fan: %.1f%% | Lid: %s | State: %s\n",
            (unsigned long)s.timestamp_ms,
            s.pit_temp_f,
            s.setpoint_f,
            s.meat_temp_f,
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
