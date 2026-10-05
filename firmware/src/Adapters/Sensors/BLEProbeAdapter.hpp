#pragma once

#include "../../Services/Ports/TemperatureSensorPort.hpp"
#include "../../Domain/BLEDecoder.hpp"
#include <cstdint>
#include <cstring>
#include <string>
#include "../Runtime/SnapshotMutex.hpp"

#ifdef ARDUINO
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#endif

namespace SmokerController::Adapters::Sensors {

class BLEProbeAdapter : public Services::Ports::ITemperatureSensorPort
#ifdef ARDUINO
    , public BLEAdvertisedDeviceCallbacks
#endif
{
public:
    explicit BLEProbeAdapter(uint32_t staleness_timeout_ms = 30000) noexcept
        : staleness_timeout_ms_(staleness_timeout_ms),
          last_packet_time_ms_(0),
          has_received_packet_(false)
    {
        target_mac_[0] = '\0';
    }

    void setTargetMac(const char* mac) noexcept {
        Runtime::SnapshotLock lock(mutex_);
        if (mac) {
            std::strncpy(target_mac_, mac, sizeof(target_mac_) - 1);
            target_mac_[sizeof(target_mac_) - 1] = '\0';
        } else {
            target_mac_[0] = '\0';
        }
    }

    [[nodiscard]] std::string targetMac() const {
        char copy[sizeof(target_mac_)];
        { Runtime::SnapshotLock lock(mutex_); std::memcpy(copy, target_mac_, sizeof(copy)); }
        return copy;
    }
    [[nodiscard]] Domain::BLEProbeReading lastReading() const noexcept { Runtime::SnapshotLock lock(mutex_); return last_reading_; }
    [[nodiscard]] bool isConnected(uint32_t now_ms) const noexcept {
        Runtime::SnapshotLock lock(mutex_);
        return has_received_packet_ && ((now_ms - last_packet_time_ms_) <= staleness_timeout_ms_);
    }

    void begin() noexcept {
#ifdef ARDUINO
        BLEDevice::init("SmokerController");
        BLEScan* pBLEScan = BLEDevice::getScan();
        pBLEScan->setAdvertisedDeviceCallbacks(this);
        pBLEScan->setActiveScan(false); // Passive scan uses less power & coexists with Wi-Fi
        pBLEScan->setInterval(100);
        pBLEScan->setWindow(99);
        pBLEScan->start(0, nullptr, false); // Continuous background scan
#endif
    }

    void update(uint32_t now_ms) noexcept {
        (void)now_ms;
#ifdef ARDUINO
        // BLE scan runs asynchronously on FreeRTOS task
#endif
    }

    bool processAdvertisement(const uint8_t* payload, size_t length, const char* mac, uint32_t now_ms) noexcept {
        if (!payload || length == 0) return false;

        const auto target = targetMac();
        if (!target.empty()) {
            if (!mac || target != mac) {
                return false;
            }
        }

        Domain::BLEProbeReading reading{};
        if (Domain::BLEAdvertisementDecoder::decodeAny(payload, length, reading)) {
            if (mac) {
                std::strncpy(reading.mac_address, mac, sizeof(reading.mac_address) - 1);
            }
            Runtime::SnapshotLock lock(mutex_);
            last_reading_ = reading;
            last_packet_time_ms_ = now_ms;
            has_received_packet_ = true;
            return true;
        }
        return false;
    }

#ifdef ARDUINO
    void onResult(BLEAdvertisedDevice advertisedDevice) override {
        uint32_t now = millis();
        const std::string mac = advertisedDevice.getAddress().toString();

        if (advertisedDevice.haveServiceData()) {
            std::string serviceData = advertisedDevice.getServiceData();
            if (processAdvertisement(
                reinterpret_cast<const uint8_t*>(serviceData.data()),
                serviceData.length(),
                mac.c_str(),
                now
            )) {
                return;
            }
        }

        if (advertisedDevice.haveManufacturerData()) {
            std::string mfgData = advertisedDevice.getManufacturerData();
            processAdvertisement(
                reinterpret_cast<const uint8_t*>(mfgData.data()),
                mfgData.length(),
                mac.c_str(),
                now
            );
        }
    }
#endif

    void setMockTime(uint32_t now_ms) noexcept {
        Runtime::SnapshotLock lock(mutex_);
        mock_time_ms_ = now_ms;
    }

    Domain::TemperatureReading readTemperature(Domain::SensorRole role) noexcept override {
        Runtime::SnapshotLock lock(mutex_);
        uint32_t now = 0;
#ifdef ARDUINO
        now = millis();
#else
        now = (mock_time_ms_ > 0) ? mock_time_ms_ : last_packet_time_ms_;
#endif

        if (!(has_received_packet_ && ((now - last_packet_time_ms_) <= staleness_timeout_ms_))) {
            Domain::TemperatureReading r{
                0.0f,
                role,
                now,
                has_received_packet_ ? Domain::SensorFault::Stale : Domain::SensorFault::Disconnected,
                true,
                last_reading_.battery_pct,
                ""
            };
            std::strncpy(r.probe_name, last_reading_.probe_name, sizeof(r.probe_name) - 1);
            r.probe_name[sizeof(r.probe_name) - 1] = '\0';
            return r;
        }

        if (role == Domain::SensorRole::Food1) {
            Domain::TemperatureReading r{
                last_reading_.internal_temp_c,
                role,
                last_packet_time_ms_,
                Domain::SensorFault::Ok,
                true,
                last_reading_.battery_pct,
                ""
            };
            std::strncpy(r.probe_name, last_reading_.probe_name, sizeof(r.probe_name) - 1);
            r.probe_name[sizeof(r.probe_name) - 1] = '\0';
            return r;
        } else if (role == Domain::SensorRole::Food2 || role == Domain::SensorRole::Ambient) {
            if (last_reading_.has_ambient) {
                Domain::TemperatureReading r{
                    last_reading_.ambient_temp_c,
                    role,
                    last_packet_time_ms_,
                    Domain::SensorFault::Ok,
                    true,
                    last_reading_.battery_pct,
                    ""
                };
                std::strncpy(r.probe_name, last_reading_.probe_name, sizeof(r.probe_name) - 1);
                r.probe_name[sizeof(r.probe_name) - 1] = '\0';
                return r;
            }
        }

        return Domain::TemperatureReading{0.0f, role, now, Domain::SensorFault::Disconnected, true, -1, ""};
    }

private:
    mutable Runtime::SnapshotMutex mutex_;
    uint32_t staleness_timeout_ms_;
    uint32_t last_packet_time_ms_;
    uint32_t mock_time_ms_{0};
    bool has_received_packet_;
    char target_mac_[18];
    Domain::BLEProbeReading last_reading_{};
};

} // namespace SmokerController::Adapters::Sensors
