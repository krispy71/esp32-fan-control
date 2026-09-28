#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include "Temperature.hpp"

namespace SmokerController::Domain {

enum class BLEProbeProtocol {
    BTHome,
    Inkbird,
    Meater,
    SigEnvironmental,
    Unknown
};

struct BLEProbeReading {
    float internal_temp_c{0.0f};
    float ambient_temp_c{0.0f};
    bool has_ambient{false};
    int8_t battery_pct{-1}; // -1 = unknown
    BLEProbeProtocol protocol{BLEProbeProtocol::Unknown};
    char mac_address[18]{""};
    char probe_name[32]{"Wireless Probe"};

    [[nodiscard]] constexpr float internalFahrenheit() const noexcept {
        return (internal_temp_c * 9.0f / 5.0f) + 32.0f;
    }

    [[nodiscard]] constexpr float ambientFahrenheit() const noexcept {
        return (ambient_temp_c * 9.0f / 5.0f) + 32.0f;
    }
};

class BLEAdvertisementDecoder {
public:
    static bool decodeBTHomeV2(const uint8_t* payload, size_t length, BLEProbeReading& out) noexcept {
        if (!payload || length < 4) return false;
        size_t idx = 1; // Skip BTHome flag header
        bool found_temp = false;

        while (idx < length) {
            uint8_t obj_id = payload[idx++];
            if (obj_id == 0x02) { // 16-bit temperature signed (factor 0.01)
                if (idx + 2 > length) break;
                int16_t raw = static_cast<int16_t>(payload[idx] | (payload[idx + 1] << 8));
                out.internal_temp_c = static_cast<float>(raw) * 0.01f;
                found_temp = true;
                idx += 2;
            } else if (obj_id == 0x01) { // 8-bit battery %
                if (idx + 1 > length) break;
                out.battery_pct = static_cast<int8_t>(payload[idx++]);
            } else {
                break;
            }
        }

        if (found_temp) {
            out.protocol = BLEProbeProtocol::BTHome;
            std::strncpy(out.probe_name, "BTHome Probe", sizeof(out.probe_name) - 1);
            return true;
        }
        return false;
    }

    static bool decodeInkbird(const uint8_t* payload, size_t length, BLEProbeReading& out) noexcept {
        if (!payload || length < 5) return false;
        // Bytes 2-3: 16-bit signed temp in 0.1°C little-endian
        int16_t raw = static_cast<int16_t>(payload[2] | (payload[3] << 8));
        float temp_c = static_cast<float>(raw) * 0.1f;
        if (temp_c < -40.0f || temp_c > 350.0f) return false;

        out.internal_temp_c = temp_c;
        if (length >= 5 && payload[4] <= 100) {
            out.battery_pct = static_cast<int8_t>(payload[4]);
        }
        out.protocol = BLEProbeProtocol::Inkbird;
        std::strncpy(out.probe_name, "Inkbird/ThermoPro", sizeof(out.probe_name) - 1);
        return true;
    }

    static bool decodeMeater(const uint8_t* payload, size_t length, BLEProbeReading& out) noexcept {
        if (!payload || length < 7) return false;
        // Bytes 2-3: big-endian tip temp; Bytes 4-5: ambient
        int16_t raw_tip = static_cast<int16_t>((payload[2] << 8) | payload[3]);
        int16_t raw_amb = static_cast<int16_t>((payload[4] << 8) | payload[5]);

        float tip_c = (raw_tip > 0) ? static_cast<float>(raw_tip) * 0.1f : 0.0f;
        float amb_c = (raw_amb > 0) ? static_cast<float>(raw_amb) * 0.1f : 0.0f;

        if (tip_c < 0.0f || tip_c > 120.0f) return false;

        out.internal_temp_c = tip_c;
        out.ambient_temp_c = amb_c;
        out.has_ambient = (amb_c > 0.0f);
        if (length >= 7 && payload[6] <= 100) {
            out.battery_pct = static_cast<int8_t>(payload[6]);
        }
        out.protocol = BLEProbeProtocol::Meater;
        std::strncpy(out.probe_name, "MEATER Probe", sizeof(out.probe_name) - 1);
        return true;
    }

    static bool decodeMeaterGatt(const uint8_t* payload, size_t length, BLEProbeReading& out) noexcept {
        if (!payload || length < 2) return false;
        // MEATER characteristic 7EDDA774-045E-4BBF-909B-45D1991A2876
        // Bytes 0-1: tip raw (little-endian)
        uint16_t tip_raw = static_cast<uint16_t>(payload[0] | (payload[1] << 8));
        float tip_c = (static_cast<float>(tip_raw) + 8.0f) / 16.0f;
        if (tip_c < -20.0f || tip_c > 125.0f) return false;

        out.internal_temp_c = tip_c;
        out.protocol = BLEProbeProtocol::Meater;
        std::strncpy(out.probe_name, "MEATER (Direct)", sizeof(out.probe_name) - 1);
        out.probe_name[sizeof(out.probe_name) - 1] = '\0';

        if (length >= 6) {
            uint16_t ra = static_cast<uint16_t>(payload[2] | (payload[3] << 8));
            uint16_t oa = static_cast<uint16_t>(payload[4] | (payload[5] << 8));
            int32_t min_oa = (oa < 48) ? oa : 48;
            int32_t diff = static_cast<int32_t>(ra) - min_oa;
            if (diff < 0) diff = 0;
            int32_t amb_adj = (diff * 16 * 589) / 1487;
            float amb_c = (static_cast<float>(tip_raw + amb_adj) + 8.0f) / 16.0f;
            if (amb_c >= -20.0f && amb_c <= 350.0f) {
                out.ambient_temp_c = amb_c;
                out.has_ambient = true;
            }
        }
        if (length >= 8) {
            if (payload[7] <= 100) {
                out.battery_pct = static_cast<int8_t>(payload[7]);
            }
        }
        return true;
    }

    static bool decodeMeaterCloudJson(const char* json_str, BLEProbeReading& out) noexcept {
        if (!json_str) return false;
        const char* p_int = std::strstr(json_str, "\"internal\":");
        if (!p_int) return false;
        float internal_c = 0.0f;
        if (std::sscanf(p_int + 11, "%f", &internal_c) != 1) return false;

        out.internal_temp_c = internal_c;
        out.protocol = BLEProbeProtocol::Meater;
        std::strncpy(out.probe_name, "MEATER (Cloud)", sizeof(out.probe_name) - 1);
        out.probe_name[sizeof(out.probe_name) - 1] = '\0';

        const char* p_amb = std::strstr(json_str, "\"ambient\":");
        if (p_amb) {
            float amb_c = 0.0f;
            if (std::sscanf(p_amb + 10, "%f", &amb_c) == 1) {
                out.ambient_temp_c = amb_c;
                out.has_ambient = true;
            }
        }

        const char* p_bat = std::strstr(json_str, "\"battery\":");
        if (p_bat) {
            int bat = -1;
            if (std::sscanf(p_bat + 10, "%d", &bat) == 1 && bat >= 0 && bat <= 100) {
                out.battery_pct = static_cast<int8_t>(bat);
            }
        }
        return true;
    }

    static bool decodeSigEnvironmental(const uint8_t* payload, size_t length, BLEProbeReading& out) noexcept {
        if (!payload || length < 2) return false;
        int16_t raw = static_cast<int16_t>(payload[0] | (payload[1] << 8));
        float temp_c = static_cast<float>(raw) * 0.01f;
        if (temp_c < -40.0f || temp_c > 300.0f) return false;

        out.internal_temp_c = temp_c;
        out.protocol = BLEProbeProtocol::SigEnvironmental;
        std::strncpy(out.probe_name, "SIG BLE Probe", sizeof(out.probe_name) - 1);
        return true;
    }

    static bool decodeAny(const uint8_t* payload, size_t length, BLEProbeReading& out) noexcept {
        if (decodeBTHomeV2(payload, length, out)) return true;
        if (decodeMeater(payload, length, out)) return true;
        if (decodeInkbird(payload, length, out)) return true;
        return decodeSigEnvironmental(payload, length, out);
    }
};

} // namespace SmokerController::Domain
