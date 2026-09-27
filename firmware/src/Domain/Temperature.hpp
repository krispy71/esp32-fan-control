#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace SmokerController::Domain {

enum class SensorRole {
    Pit,
    Food1,
    Food2,
    Ambient
};

enum class SensorFault {
    Ok,
    Disconnected,
    ShortToVcc,
    ShortToGnd,
    OutOfRange,
    Stale
};

struct TemperatureReading {
    float celsius{0.0f};
    SensorRole role{SensorRole::Pit};
    uint32_t timestamp_ms{0};
    SensorFault fault{SensorFault::Ok};
    bool is_wireless{false};
    int8_t battery_pct{-1}; // -1 = unknown or wired
    char probe_name[32]{""};

    [[nodiscard]] constexpr float fahrenheit() const noexcept {
        return (celsius * 9.0f / 5.0f) + 32.0f;
    }

    [[nodiscard]] static TemperatureReading fromFahrenheit(
        float deg_f,
        SensorRole sensor_role,
        uint32_t ts_ms,
        SensorFault sensor_fault = SensorFault::Ok,
        bool wireless = false,
        int8_t batt = -1,
        const char* name = ""
    ) noexcept {
        float deg_c = (deg_f - 32.0f) * 5.0f / 9.0f;
        TemperatureReading r{deg_c, sensor_role, ts_ms, sensor_fault, wireless, batt, ""};
        if (name && name[0] != '\0') {
            std::strncpy(r.probe_name, name, sizeof(r.probe_name) - 1);
            r.probe_name[sizeof(r.probe_name) - 1] = '\0';
        }
        return r;
    }

    [[nodiscard]] bool isValid() const noexcept {
        return (fault == SensorFault::Ok) && (celsius >= -40.0f) && (celsius <= 500.0f);
    }
};

} // namespace SmokerController::Domain
