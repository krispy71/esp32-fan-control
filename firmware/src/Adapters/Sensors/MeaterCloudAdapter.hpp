#pragma once

#include "../../Services/Ports/TemperatureSensorPort.hpp"
#include "../../Domain/BLEDecoder.hpp"
#include <cstdint>
#include <cstring>
#include <cstdio>

#ifdef ARDUINO
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#endif

namespace SmokerController::Adapters::Sensors {

/**
 * MeaterCloudAdapter fetches live probe temperatures from the official
 * MEATER Cloud Public REST API (https://public-api.cloud.meater.com/v1/devices).
 *
 * This allows the user to keep the official MEATER mobile app connected
 * to the probe/charger dock while the smoker controller queries readings over Wi-Fi.
 */
class MeaterCloudAdapter : public Services::Ports::ITemperatureSensorPort {
public:
    explicit MeaterCloudAdapter(uint32_t staleness_timeout_ms = 60000, uint32_t poll_interval_ms = 20000) noexcept
        : staleness_timeout_ms_(staleness_timeout_ms),
          poll_interval_ms_(poll_interval_ms),
          last_fetch_time_ms_(0),
          last_poll_attempt_ms_(0),
          has_received_data_(false),
          is_enabled_(false),
          mock_time_ms_(0)
    {
        api_token_[0] = '\0';
    }

    void setApiToken(const char* token) noexcept {
        if (token) {
            std::strncpy(api_token_, token, sizeof(api_token_) - 1);
            api_token_[sizeof(api_token_) - 1] = '\0';
        } else {
            api_token_[0] = '\0';
        }
    }

    [[nodiscard]] const char* apiToken() const noexcept { return api_token_; }

    void setEnabled(bool enabled) noexcept {
        is_enabled_ = enabled;
    }

    [[nodiscard]] bool isEnabled() const noexcept { return is_enabled_; }

    [[nodiscard]] bool isConnected(uint32_t now_ms) const noexcept {
        return has_received_data_ && ((now_ms - last_fetch_time_ms_) <= staleness_timeout_ms_);
    }

    void update(uint32_t now_ms) noexcept {
        (void)now_ms;
#ifdef ARDUINO
        if (!is_enabled_ || api_token_[0] == '\0') return;
        if (WiFi.status() != WL_CONNECTED) return;

        // Poll at configured interval (respecting MEATER rate limits)
        if (now_ms - last_poll_attempt_ms_ >= poll_interval_ms_) {
            last_poll_attempt_ms_ = now_ms;
            fetchFromCloud(now_ms);
        }
#endif
    }

    bool processCloudJson(const char* json_str, uint32_t now_ms) noexcept {
        if (!json_str) return false;
        Domain::BLEProbeReading r{};
        if (Domain::BLEAdvertisementDecoder::decodeMeaterCloudJson(json_str, r)) {
            last_reading_ = r;
            last_fetch_time_ms_ = now_ms;
            has_received_data_ = true;
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
        now = (mock_time_ms_ > 0) ? mock_time_ms_ : last_fetch_time_ms_;
#endif

        if (!isConnected(now)) {
            Domain::TemperatureReading r{
                0.0f,
                role,
                now,
                has_received_data_ ? Domain::SensorFault::Stale : Domain::SensorFault::Disconnected,
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
                last_fetch_time_ms_,
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
                    last_fetch_time_ms_,
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
    void fetchFromCloud(uint32_t now_ms) {
        WiFiClientSecure client;
        client.setInsecure(); // Skip certificate bundle check to save flash/RAM

        HTTPClient http;
        if (http.begin(client, "https://public-api.cloud.meater.com/v1/devices")) {
            char auth_header[128];
            snprintf(auth_header, sizeof(auth_header), "Bearer %s", api_token_);
            http.addHeader("Authorization", auth_header);
            http.setTimeout(5000);

            int httpCode = http.GET();
            if (httpCode == HTTP_CODE_OK) {
                String payload = http.getString();
                processCloudJson(payload.c_str(), now_ms);
            }
            http.end();
        }
    }
#endif

private:
    uint32_t staleness_timeout_ms_;
    uint32_t poll_interval_ms_;
    uint32_t last_fetch_time_ms_;
    uint32_t last_poll_attempt_ms_;
    bool has_received_data_;
    bool is_enabled_;
    uint32_t mock_time_ms_;
    char api_token_[96];
    Domain::BLEProbeReading last_reading_{};
};

} // namespace SmokerController::Adapters::Sensors
