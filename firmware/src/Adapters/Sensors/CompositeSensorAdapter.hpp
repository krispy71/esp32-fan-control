#pragma once

#include "../../Services/Ports/TemperatureSensorPort.hpp"
#include "../../Domain/Configuration.hpp"

namespace SmokerController::Adapters::Sensors {

/**
 * CompositeSensorAdapter multiplexes wired MAX31855 thermocouple readings
 * and selectable wireless probe options:
 *   1. Passive BLE Broadcast (Inkbird, BTHome, ThermoPro)
 *   2. Direct MEATER (Active BLE GATT)
 *   3. MEATER Cloud REST API (Wi-Fi sync)
 *   4. Wired Only (MAX31855 Food probe)
 *
 * Rules:
 *  - Chamber/Pit temperature is always routed to the wired thermocouple.
 *  - Food/Meat temperature is routed to the configured MeatProbeMode.
 *    If the chosen wireless source is disconnected or stale, it automatically
 *    falls back to the secondary wired food probe failsafe.
 */
class CompositeSensorAdapter : public Services::Ports::ITemperatureSensorPort {
public:
    explicit CompositeSensorAdapter(
        Services::Ports::ITemperatureSensorPort& wired_sensor,
        Services::Ports::ITemperatureSensorPort* passive_ble_sensor = nullptr,
        Services::Ports::ITemperatureSensorPort* meater_direct_sensor = nullptr,
        Services::Ports::ITemperatureSensorPort* meater_cloud_sensor = nullptr
    ) noexcept
        : wired_sensor_(wired_sensor),
          passive_ble_sensor_(passive_ble_sensor),
          meater_direct_sensor_(meater_direct_sensor),
          meater_cloud_sensor_(meater_cloud_sensor),
          mode_(Domain::MeatProbeMode::PassiveBle)
    {}

    void setMode(Domain::MeatProbeMode mode) noexcept {
        mode_ = mode;
    }

    [[nodiscard]] Domain::MeatProbeMode mode() const noexcept {
        return mode_;
    }

    void setWirelessSensor(Services::Ports::ITemperatureSensorPort* wireless_sensor) noexcept {
        passive_ble_sensor_ = wireless_sensor;
    }

    void setPassiveBleSensor(Services::Ports::ITemperatureSensorPort* sensor) noexcept {
        passive_ble_sensor_ = sensor;
    }

    void setMeaterDirectSensor(Services::Ports::ITemperatureSensorPort* sensor) noexcept {
        meater_direct_sensor_ = sensor;
    }

    void setMeaterCloudSensor(Services::Ports::ITemperatureSensorPort* sensor) noexcept {
        meater_cloud_sensor_ = sensor;
    }

    [[nodiscard]] bool hasWirelessSensor() const noexcept {
        return (passive_ble_sensor_ != nullptr) ||
               (meater_direct_sensor_ != nullptr) ||
               (meater_cloud_sensor_ != nullptr);
    }

    Domain::TemperatureReading readTemperature(Domain::SensorRole role) noexcept override {
        // Pit probe must always be wired to withstand smoker chamber temps
        if (role == Domain::SensorRole::Pit) {
            return wired_sensor_.readTemperature(role);
        }

        // Meat/Food role: query configured wireless adapter
        if (role == Domain::SensorRole::Food1) {
            Services::Ports::ITemperatureSensorPort* active_sensor = nullptr;
            switch (mode_) {
                case Domain::MeatProbeMode::PassiveBle:
                    active_sensor = passive_ble_sensor_;
                    break;
                case Domain::MeatProbeMode::MeaterBleDirect:
                    active_sensor = meater_direct_sensor_;
                    break;
                case Domain::MeatProbeMode::MeaterCloud:
                    active_sensor = meater_cloud_sensor_;
                    break;
                case Domain::MeatProbeMode::WiredOnly:
                default:
                    active_sensor = nullptr;
                    break;
            }

            if (active_sensor != nullptr) {
                auto reading = active_sensor->readTemperature(role);
                if (reading.isValid()) {
                    return reading;
                }
            }

            // Fallback to wired sensor if wireless is unconfigured, disconnected, or stale
            return wired_sensor_.readTemperature(role);
        }

        // Ambient / Food 2 query
        if (role == Domain::SensorRole::Ambient || role == Domain::SensorRole::Food2) {
            Services::Ports::ITemperatureSensorPort* active_sensor = nullptr;
            if (mode_ == Domain::MeatProbeMode::MeaterBleDirect) active_sensor = meater_direct_sensor_;
            else if (mode_ == Domain::MeatProbeMode::MeaterCloud) active_sensor = meater_cloud_sensor_;
            else if (mode_ == Domain::MeatProbeMode::PassiveBle) active_sensor = passive_ble_sensor_;

            if (active_sensor != nullptr) {
                auto reading = active_sensor->readTemperature(role);
                if (reading.isValid()) {
                    return reading;
                }
            }
        }

        return wired_sensor_.readTemperature(role);
    }

private:
    Services::Ports::ITemperatureSensorPort& wired_sensor_;
    Services::Ports::ITemperatureSensorPort* passive_ble_sensor_;
    Services::Ports::ITemperatureSensorPort* meater_direct_sensor_;
    Services::Ports::ITemperatureSensorPort* meater_cloud_sensor_;
    Domain::MeatProbeMode mode_;
};

} // namespace SmokerController::Adapters::Sensors
