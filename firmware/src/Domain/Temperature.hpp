#pragma once

#include <cstdint>

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
    float celsius;
    SensorRole role;
    uint32_t timestamp_ms;
    SensorFault fault{SensorFault::Ok};

    [[nodiscard]] constexpr float fahrenheit() const noexcept {
        return (celsius * 9.0f / 5.0f) + 32.0f;
    }

    [[nodiscard]] static TemperatureReading fromFahrenheit(
        float deg_f,
        SensorRole sensor_role,
        uint32_t ts_ms,
        SensorFault sensor_fault = SensorFault::Ok
    ) noexcept {
        float deg_c = (deg_f - 32.0f) * 5.0f / 9.0f;
        return TemperatureReading{deg_c, sensor_role, ts_ms, sensor_fault};
    }

    [[nodiscard]] bool isValid() const noexcept {
        return (fault == SensorFault::Ok) && (celsius >= -40.0f) && (celsius <= 500.0f);
    }
};

} // namespace SmokerController::Domain
