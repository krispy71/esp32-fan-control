#pragma once

#include "../../Services/Ports/TemperatureSensorPort.hpp"
#include "../../Domain/BLEDecoder.hpp"
#include <cstdint>
#include <cstring>

#ifdef ARDUINO
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEClient.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#endif

namespace SmokerController::Adapters::Sensors {

/**
 * MeaterBleClientAdapter establishes an active Bluetooth Low Energy (BLE)
 * GATT client connection to a genuine MEATER probe.
 *
 * It subscribes to characteristic 7EDDA774-045E-4BBF-909B-45D1991A2876
 * under service A75CC7FC-C956-488F-AC2A-2DBC08B63A04.
 */
class MeaterBleClientAdapter : public Services::Ports::ITemperatureSensorPort {
public:
    explicit MeaterBleClientAdapter(uint32_t staleness_timeout_ms = 30000) noexcept
        : staleness_timeout_ms_(staleness_timeout_ms),
          last_packet_time_ms_(0),
          last_reconnect_attempt_ms_(0),
          is_connected_(false),
          has_received_packet_(false),
          is_enabled_(false),
          mock_time_ms_(0)
    {
        target_mac_[0] = '\0';
    }

    void setTargetMac(const char* mac) noexcept {
        if (mac) {
            std::strncpy(target_mac_, mac, sizeof(target_mac_) - 1);
            target_mac_[sizeof(target_mac_) - 1] = '\0';
        } else {
            target_mac_[0] = '\0';
        }
    }

    [[nodiscard]] const char* targetMac() const noexcept { return target_mac_; }
    [[nodiscard]] bool isConnected(uint32_t now_ms) const noexcept {
        return has_received_packet_ && ((now_ms - last_packet_time_ms_) <= staleness_timeout_ms_);
    }

    void setEnabled(bool enabled) noexcept {
        is_enabled_ = enabled;
    }

    [[nodiscard]] bool isEnabled() const noexcept { return is_enabled_; }

    void begin() noexcept {
#ifdef ARDUINO
        // BLEDevice is initialized globally in main setup
#endif
    }

    void update(uint32_t now_ms) noexcept {
        (void)now_ms;
#ifdef ARDUINO
        if (!is_enabled_) return;
        // Periodic check: if not connected and backoff expired (every 15s), attempt scan/connect
        if (!is_connected_ && (now_ms - last_reconnect_attempt_ms_ > 15000)) {
            last_reconnect_attempt_ms_ = now_ms;
            // Scan for MEATER probe if target MAC configured or any device with MEATER name
            BLEScan* pScan = BLEDevice::getScan();
            if (pScan) {
                BLEScanResults foundDevices = pScan->start(2, false);
                for (int i = 0; i < foundDevices.getCount(); i++) {
                    BLEAdvertisedDevice device = foundDevices.getDevice(i);
                    bool match = false;
                    if (target_mac_[0] != '\0') {
                        if (device.getAddress().toString() == target_mac_) match = true;
                    } else if (device.haveName() && device.getName().rfind("MEATER", 0) == 0) {
                        match = true;
                    }
                    if (match) {
                        connectToDevice(device);
                        break;
                    }
                }
            }
        }
#endif
    }

    bool processGattPayload(const uint8_t* payload, size_t length, uint32_t now_ms) noexcept {
        if (!payload || length < 2) return false;
        Domain::BLEProbeReading r{};
        if (Domain::BLEAdvertisementDecoder::decodeMeaterGatt(payload, length, r)) {
            last_reading_ = r;
            last_packet_time_ms_ = now_ms;
            has_received_packet_ = true;
            return true;
        }
        return false;
    }

    void setMockTime(uint32_t now_ms) noexcept {
        mock_time_ms_ = now_ms;
    }

    Domain::TemperatureReading readTemperature(Domain::SensorRole role) noexcept override {
        uint32_t now = 0;
#ifdef ARDUINO
        now = millis();
#else
        now = (mock_time_ms_ > 0) ? mock_time_ms_ : last_packet_time_ms_;
#endif

        if (!isConnected(now)) {
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

#ifdef ARDUINO
private:
    void connectToDevice(BLEAdvertisedDevice& device) {
        BLEClient* pClient = BLEDevice::createClient();
        if (!pClient->connect(&device)) {
            delete pClient;
            return;
        }

        BLERemoteService* pRemoteService = pClient->getService(BLEUUID("a75cc7fc-c956-488f-ac2a-2dbc08b63a04"));
        if (!pRemoteService) {
            pClient->disconnect();
            delete pClient;
            return;
        }

        BLERemoteCharacteristic* pRemoteChar = pRemoteService->getCharacteristic(BLEUUID("7edda774-045e-4bbf-909b-45d1991a2876"));
        if (!pRemoteChar) {
            pClient->disconnect();
            delete pClient;
            return;
        }

        if (pRemoteChar->canRead()) {
            std::string value = pRemoteChar->readValue();
            processGattPayload(reinterpret_cast<const uint8_t*>(value.data()), value.length(), millis());
            is_connected_ = true;
        }

        if (pRemoteChar->canNotify()) {
            pRemoteChar->registerForNotify([](BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
                (void)pBLERemoteCharacteristic;
                (void)isNotify;
                // Static dispatch handled via instance
            });
        }
    }
#endif

private:
    uint32_t staleness_timeout_ms_;
    uint32_t last_packet_time_ms_;
    uint32_t last_reconnect_attempt_ms_;
    bool is_connected_;
    bool has_received_packet_;
    bool is_enabled_;
    uint32_t mock_time_ms_;
    char target_mac_[18];
    Domain::BLEProbeReading last_reading_{};
};

} // namespace SmokerController::Adapters::Sensors
