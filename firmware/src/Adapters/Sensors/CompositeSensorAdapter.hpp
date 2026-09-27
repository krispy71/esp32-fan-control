#pragma once

#include "../../Services/Ports/TemperatureSensorPort.hpp"

namespace SmokerController::Adapters::Sensors {

/**
 * CompositeSensorAdapter multiplexes wired MAX31855 thermocouple readings
 * and optional Bluetooth Low Energy wireless probes.
 *
 * Rules:
 *  - Chamber/Pit temperature is always routed to the wired thermocouple.
 *  - Food/Meat temperature prefers the wireless probe when active and valid;
 *    falls back to the secondary wired food probe when wireless is disconnected or stale.
 */
class CompositeSensorAdapter : public Services::Ports::ITemperatureSensorPort {
public:
    explicit CompositeSensorAdapter(
        Services::Ports::ITemperatureSensorPort& wired_sensor,
        Services::Ports::ITemperatureSensorPort* wireless_sensor = nullptr
    ) noexcept
        : wired_sensor_(wired_sensor),
          wireless_sensor_(wireless_sensor)
    {}

    void setWirelessSensor(Services::Ports::ITemperatureSensorPort* wireless_sensor) noexcept {
        wireless_sensor_ = wireless_sensor;
    }

    [[nodiscard]] bool hasWirelessSensor() const noexcept {
        return wireless_sensor_ != nullptr;
    }

    Domain::TemperatureReading readTemperature(Domain::SensorRole role) noexcept override {
        // Pit probe must always be wired to withstand smoker chamber temps
        if (role == Domain::SensorRole::Pit) {
            return wired_sensor_.readTemperature(role);
        }

        // Food probes prefer wireless when available and healthy
        if (wireless_sensor_ != nullptr) {
            auto reading = wireless_sensor_->readTemperature(role);
            if (reading.isValid()) {
                return reading;
            }
        }

        // Fallback to wired sensor
        return wired_sensor_.readTemperature(role);
    }

private:
    Services::Ports::ITemperatureSensorPort& wired_sensor_;
    Services::Ports::ITemperatureSensorPort* wireless_sensor_;
};

} // namespace SmokerController::Adapters::Sensors
