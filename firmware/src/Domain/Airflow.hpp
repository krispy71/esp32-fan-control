#pragma once

#include <algorithm>

namespace SmokerController::Domain {

struct AirflowDemand {
    float value_pct;

    constexpr explicit AirflowDemand(float v = 0.0f) noexcept
        : value_pct(std::clamp(v, 0.0f, 100.0f)) {}
};

struct ActuatorTargets {
    float damper_position_pct; // 0.0 = fully closed, 100.0 = fully open
    float blower_speed_pct;    // 0.0 = off, 100.0 = full RPM

    constexpr ActuatorTargets(float damper = 0.0f, float blower = 0.0f) noexcept
        : damper_position_pct(std::clamp(damper, 0.0f, 100.0f)),
          blower_speed_pct(std::clamp(blower, 0.0f, 100.0f)) {}
};

class ActuatorCoordinator {
public:
    constexpr explicit ActuatorCoordinator(
        float blower_threshold_pct = 40.0f,
        float min_blower_speed_pct = 10.0f
    ) noexcept
        : threshold_(std::clamp(blower_threshold_pct, 1.0f, 99.0f)),
          min_blower_(std::clamp(min_blower_speed_pct, 0.0f, 99.0f)) {}

    [[nodiscard]] constexpr float blowerThreshold() const noexcept { return threshold_; }
    [[nodiscard]] constexpr float minBlowerSpeed() const noexcept { return min_blower_; }

    [[nodiscard]] ActuatorTargets coordinate(const AirflowDemand& demand) const noexcept {
        const float val = demand.value_pct;

        // Invariant: Zero demand forces complete closure against chimney draft
        if (val <= 0.0f) {
            return ActuatorTargets{0.0f, 0.0f};
        }

        // Stage 1: Natural draft modulation (damper opens 0-100%, blower fan off)
        if (val <= threshold_) {
            float damper = (val / threshold_) * 100.0f;
            return ActuatorTargets{damper, 0.0f};
        }

        // Stage 2: Forced draft boost (damper locked 100% open, blower modulates)
        float span = 100.0f - threshold_;
        float excess = val - threshold_;
        float ratio = excess / span;
        float blower = min_blower_ + ratio * (100.0f - min_blower_);

        return ActuatorTargets{100.0f, blower};
    }

private:
    float threshold_;
    float min_blower_;
};

} // namespace SmokerController::Domain
