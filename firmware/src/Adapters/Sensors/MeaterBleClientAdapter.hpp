#pragma once

#include "../../Services/Ports/TemperatureSensorPort.hpp"
#include "../../Domain/BLEDecoder.hpp"
#include <cstdint>
#include <cstring>
#include <string>
#include "../Runtime/SnapshotMutex.hpp"
#include "SharedBleScanner.hpp"

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
class MeaterBleClientAdapter : public Services::Ports::ITemperatureSensorPort
#ifdef ARDUINO
    , public BLEAdvertisedDeviceCallbacks
#endif
{
public:
    explicit MeaterBleClientAdapter(uint32_t staleness_timeout_ms = 30000, SharedBleScanner* scanner = nullptr) noexcept
        : scanner_(scanner), staleness_timeout_ms_(staleness_timeout_ms),
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
        Runtime::SnapshotLock lock(mutex_);
#ifdef ARDUINO
        if (std::strcmp(target_mac_, mac ? mac : "") != 0) {
            candidate_ready_ = false;
            reconnect_ = true;
            has_received_packet_ = false;
        }
#endif
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
    [[nodiscard]] bool isConnected(uint32_t now_ms) const noexcept {
        Runtime::SnapshotLock lock(mutex_);
        return has_received_packet_ && ((now_ms - last_packet_time_ms_) <= staleness_timeout_ms_);
    }

    void setEnabled(bool enabled) noexcept {
        Runtime::SnapshotLock lock(mutex_);
        is_enabled_ = enabled;
#ifdef ARDUINO
        if (!enabled) candidate_ready_ = false;
#endif
    }

    [[nodiscard]] bool isEnabled() const noexcept { Runtime::SnapshotLock lock(mutex_); return is_enabled_; }

    void begin() noexcept {
#ifdef ARDUINO
        // SharedBleScanner initializes the SDK before observations begin.
#endif
    }

    void update(uint32_t now_ms) noexcept {
        (void)now_ms;
#ifdef ARDUINO
        bool reconnect = false;
        { Runtime::SnapshotLock lock(mutex_); reconnect = reconnect_; reconnect_ = false; }
        if (!isEnabled() || reconnect) {
            if (client_ && client_->isConnected()) client_->disconnect();
            is_connected_ = false;
            return;
        }
        if (client_ && !client_->isConnected()) is_connected_ = false;
        // The shared passive scanner publishes a single matching candidate.
        // Never ask Arduino for a retained, unbounded scan result collection.
        if (!is_connected_ && (now_ms - last_reconnect_attempt_ms_ > 15000)) {
            if (!scanner_) return;
            char mac[18]{};
            uint8_t address_type = 0;
            {
                Runtime::SnapshotLock lock(mutex_);
                if (!candidate_ready_ || now_ms - candidate_time_ms_ > 15000) return;
                std::memcpy(mac, candidate_mac_, sizeof(mac));
                address_type = candidate_address_type_;
                candidate_ready_ = false;
            }
            last_reconnect_attempt_ms_ = now_ms;
            scanner_->pause();
            connectToDevice(mac, address_type);
            scanner_->resume();
        }
#endif
    }

    bool processGattPayload(const uint8_t* payload, size_t length, uint32_t now_ms) noexcept {
        if (!payload || length < 2) return false;
        Domain::BLEProbeReading r{};
        if (Domain::BLEAdvertisementDecoder::decodeMeaterGatt(payload, length, r)) {
            Runtime::SnapshotLock lock(mutex_);
            last_reading_ = r;
            last_packet_time_ms_ = now_ms;
            has_received_packet_ = true;
            return true;
        }
        return false;
    }

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

#ifdef ARDUINO
    void onResult(BLEAdvertisedDevice device) override {
        const auto mac = device.getAddress().toString();
        const auto name = device.haveName() ? device.getName() : std::string{};
        Runtime::SnapshotLock lock(mutex_);
        if (!is_enabled_ || mac.size() != 17) return;
        if (target_mac_[0] ? mac != target_mac_ : name.rfind("MEATER", 0) != 0) return;
        std::memcpy(candidate_mac_, mac.c_str(), sizeof(candidate_mac_));
        candidate_address_type_ = device.getAddressType();
        candidate_time_ms_ = millis();
        candidate_ready_ = true;
    }

private:
    void connectToDevice(const char* mac, uint8_t address_type) {
        if (!client_) client_ = BLEDevice::createClient();
        BLEClient* pClient = client_;
        if (!pClient) return;
        if (!pClient->connect(BLEAddress(std::string(mac)), static_cast<esp_ble_addr_type_t>(address_type))) {
            return;
        }

        // Arduino preserves service/characteristic handles across reconnects.
        // Discover this peer before selecting any cached service or handle.
        pClient->getServices();

        BLERemoteService* pRemoteService = pClient->getService(BLEUUID("a75cc7fc-c956-488f-ac2a-2dbc08b63a04"));
        if (!pRemoteService) {
            pClient->disconnect();
            return;
        }

        BLERemoteCharacteristic* pRemoteChar = pRemoteService->getCharacteristic(BLEUUID("7edda774-045e-4bbf-909b-45d1991a2876"));
        if (!pRemoteChar) {
            pClient->disconnect();
            return;
        }

        if (pRemoteChar->canRead()) {
            std::string value = pRemoteChar->readValue();
            processGattPayload(reinterpret_cast<const uint8_t*>(value.data()), value.length(), millis());
            is_connected_ = true;
        }

        if (pRemoteChar->canNotify()) {
            pRemoteChar->registerForNotify([this](BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
                (void)pBLERemoteCharacteristic;
                (void)isNotify;
                processGattPayload(pData, length, millis());
            });
        }
    }
#endif

private:
#ifdef ARDUINO
    BLEClient* client_{nullptr}; // Arduino BLE owns its client registry for device lifetime.
    char candidate_mac_[18]{};
    uint8_t candidate_address_type_{0};
    uint32_t candidate_time_ms_{0};
    bool candidate_ready_{false}, reconnect_{false};
#endif
    mutable Runtime::SnapshotMutex mutex_;
    SharedBleScanner* scanner_;
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
