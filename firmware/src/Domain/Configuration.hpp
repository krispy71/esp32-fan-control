#pragma once

#include <cstdint>
#include <cstring>

namespace SmokerController::Domain {

enum class MeatProbeMode : uint8_t {
    WiredOnly = 0,
    PassiveBle = 1,
    MeaterBleDirect = 2,
    MeaterCloud = 3
};

struct DamperCalibration {
    uint16_t min_pulse_us{1000};
    uint16_t max_pulse_us{2000};
    bool inverted{false};

    [[nodiscard]] bool isValid() const noexcept {
        return min_pulse_us >= 500 && max_pulse_us <= 2500 && min_pulse_us < max_pulse_us;
    }
};

struct SmokerConfig {
    float setpoint_f{225.0f};
    float pid_kp{3.0f};
    float pid_ki{0.02f};
    float pid_kd{15.0f};
    float airflow_threshold_pct{40.0f};
    float lid_drop_threshold_deg{15.0f};
    uint32_t lid_pause_duration_ms{180000}; // 180 seconds
    MeatProbeMode meat_probe_mode{MeatProbeMode::PassiveBle};
    char meater_cloud_token[96]{""};
    char meater_mac_filter[18]{""};

    uint16_t servo_min_pulse_us{1000};
    uint16_t servo_max_pulse_us{2000};
    bool servo_inverted{false};

    [[nodiscard]] DamperCalibration damperCalibration() const noexcept {
        return {servo_min_pulse_us, servo_max_pulse_us, servo_inverted};
    }

    [[nodiscard]] bool isValid() const noexcept {
        return (setpoint_f >= 100.0f && setpoint_f <= 450.0f) &&
               (pid_kp >= 0.0f && pid_kp <= 100.0f) &&
               (pid_ki >= 0.0f && pid_ki <= 10.0f) &&
               (pid_kd >= 0.0f && pid_kd <= 500.0f) &&
               (airflow_threshold_pct >= 10.0f && airflow_threshold_pct <= 90.0f) &&
               (lid_drop_threshold_deg >= 5.0f && lid_drop_threshold_deg <= 50.0f) &&
               (lid_pause_duration_ms >= 10000 && lid_pause_duration_ms <= 600000) &&
               (static_cast<uint8_t>(meat_probe_mode) <= 3) &&
               damperCalibration().isValid() &&
               std::memchr(meater_cloud_token, '\0', sizeof(meater_cloud_token)) != nullptr &&
               std::memchr(meater_mac_filter, '\0', sizeof(meater_mac_filter)) != nullptr;
    }
};

} // namespace SmokerController::Domain
