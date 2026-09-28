#pragma once

#include <cstdint>

namespace SmokerController::Domain {

enum class MeatProbeMode : uint8_t {
    WiredOnly = 0,
    PassiveBle = 1,
    MeaterBleDirect = 2,
    MeaterCloud = 3
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

    [[nodiscard]] bool isValid() const noexcept {
        return (setpoint_f >= 100.0f && setpoint_f <= 450.0f) &&
               (pid_kp >= 0.0f && pid_kp <= 100.0f) &&
               (pid_ki >= 0.0f && pid_ki <= 10.0f) &&
               (pid_kd >= 0.0f && pid_kd <= 500.0f) &&
               (airflow_threshold_pct >= 10.0f && airflow_threshold_pct <= 90.0f) &&
               (lid_drop_threshold_deg >= 5.0f && lid_drop_threshold_deg <= 50.0f) &&
               (lid_pause_duration_ms >= 10000 && lid_pause_duration_ms <= 600000) &&
               (static_cast<uint8_t>(meat_probe_mode) <= 3);
    }
};

} // namespace SmokerController::Domain
