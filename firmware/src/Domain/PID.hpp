#pragma once

#include <algorithm>
#include <cstdint>
#include "Airflow.hpp"

namespace SmokerController::Domain {

struct PIDConfig {
    float kp{3.0f};
    float ki{0.02f};
    float kd{15.0f};
    float integral_min{0.0f};
    float integral_max{100.0f};
    float output_min{0.0f};
    float output_max{100.0f};
    float derivative_alpha{0.8f}; // Low-pass filter coefficient for derivative term
};

class PIDRegulator {
public:
    explicit PIDRegulator(float target_setpoint, const PIDConfig& config = {}) noexcept
        : setpoint_(target_setpoint),
          config_(config),
          integral_(0.0f),
          last_error_(0.0f),
          last_time_ms_(0),
          filtered_derivative_(0.0f),
          initialized_(false) {}

    [[nodiscard]] float setpoint() const noexcept { return setpoint_; }
    void setSetpoint(float value) noexcept { setpoint_ = value; }

    [[nodiscard]] const PIDConfig& config() const noexcept { return config_; }
    void setConfig(const PIDConfig& cfg) noexcept { config_ = cfg; }

    [[nodiscard]] float integral() const noexcept { return integral_; }

    void reset() noexcept {
        integral_ = 0.0f;
        last_error_ = 0.0f;
        last_time_ms_ = 0;
        filtered_derivative_ = 0.0f;
        initialized_ = false;
    }

    AirflowDemand compute(float current_temp, uint32_t current_time_ms) noexcept {
        const float error = setpoint_ - current_temp;

        if (!initialized_) {
            initialized_ = true;
            last_time_ms_ = current_time_ms;
            last_error_ = error;
            float p_term = config_.kp * error;
            return AirflowDemand{std::clamp(p_term, config_.output_min, config_.output_max)};
        }

        float dt = static_cast<float>(current_time_ms - last_time_ms_) / 1000.0f;
        if (dt <= 0.0f) {
            dt = 0.001f;
        }

        // 1. Proportional term
        float p_term = config_.kp * error;

        // 2. Integral term with anti-windup accumulator clamp
        integral_ += (error * dt);
        integral_ = std::clamp(integral_, config_.integral_min, config_.integral_max);
        float i_term = config_.ki * integral_;

        // 3. Filtered derivative term
        float raw_derivative = (error - last_error_) / dt;
        float alpha = config_.derivative_alpha;
        filtered_derivative_ = (alpha * filtered_derivative_) + ((1.0f - alpha) * raw_derivative);
        float d_term = config_.kd * filtered_derivative_;

        // 4. Output with saturation clamp
        float total = p_term + i_term + d_term;
        float output = std::clamp(total, config_.output_min, config_.output_max);

        last_error_ = error;
        last_time_ms_ = current_time_ms;

        return AirflowDemand{output};
    }

private:
    float setpoint_;
    PIDConfig config_;
    float integral_;
    float last_error_;
    uint32_t last_time_ms_;
    float filtered_derivative_;
    bool initialized_;
};

} // namespace SmokerController::Domain
