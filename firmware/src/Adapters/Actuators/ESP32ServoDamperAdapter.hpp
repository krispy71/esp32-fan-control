#pragma once

#include "../../Services/Ports/DamperActuatorPort.hpp"
#include <algorithm>
#include <cstdint>
#include <cmath>

#ifdef ARDUINO
#include <Arduino.h>
#include <esp32-hal-ledc.h>
#endif

namespace SmokerController::Adapters::Actuators {

class ESP32ServoDamperAdapter : public Services::Ports::IDamperActuatorPort {
public:
    ESP32ServoDamperAdapter(
        uint8_t gpio_pin = 26,
        uint8_t ledc_channel = 2,
        uint32_t min_pulse_us = 1000,
        uint32_t max_pulse_us = 2000,
        bool invert_direction = false,
        uint32_t idle_timeout_ms = 1500
    ) noexcept
        : pin_(gpio_pin),
          channel_(ledc_channel),
          min_us_(min_pulse_us),
          max_us_(max_pulse_us),
          invert_(invert_direction),
          idle_timeout_ms_(idle_timeout_ms),
          current_position_pct_(0.0f),
          last_moved_ms_(0),
          is_attached_(false) {}

    void configure(const Domain::DamperCalibration& calibration) noexcept override {
        if (!calibration.isValid()) return;
        const bool changed = min_us_ != calibration.min_pulse_us ||
            max_us_ != calibration.max_pulse_us || invert_ != calibration.inverted;
        min_us_ = calibration.min_pulse_us;
        max_us_ = calibration.max_pulse_us;
        invert_ = calibration.inverted;
        if (begun_ && changed) drive(current_position_pct_);
    }

    void begin() noexcept {
#ifdef ARDUINO
        // ESP32 channels 0/1 share timer 0; channel 2 uses independent timer 1.
        ledcSetup(channel_, 50, 16);
#endif
        begun_ = true;
        drive(0.0f); // Always emit the calibrated first pulse, including inversion.
    }

    void setPosition(float position_pct) noexcept override {
        const float clamped = std::isfinite(position_pct) ? std::clamp(position_pct, 0.0f, 100.0f) : 0.0f;
        if (!begun_) { current_position_pct_ = clamped; return; }
        // An unchanged target must not undo idle detach on every control cycle.
        if (std::abs(clamped - current_position_pct_) > 0.4f) drive(clamped);
    }

    /// Periodic housekeeping: auto-detaches PWM output when stationary to eliminate servo buzz
    void update(uint32_t current_time_ms) noexcept {
#ifdef ARDUINO
        if (is_attached_ && (current_time_ms - last_moved_ms_ >= idle_timeout_ms_)) {
            // Write 0 duty to stop driving motor coils
            ledcWrite(channel_, 0);
            ledcDetachPin(pin_);
            is_attached_ = false;
        }
#else
        (void)current_time_ms;
#endif
    }

    [[nodiscard]] float currentPosition() const noexcept { return current_position_pct_; }
    [[nodiscard]] bool isAttached() const noexcept { return is_attached_; }

private:
    void drive(float position_pct) noexcept {
        current_position_pct_ = position_pct;
#ifdef ARDUINO
        if (!is_attached_) ledcAttachPin(pin_, channel_);
        last_moved_ms_ = millis();
#endif
        is_attached_ = true;
        writeMicroseconds(calculatePulseUs(position_pct));
    }

    [[nodiscard]] uint32_t calculatePulseUs(float pct) const noexcept {
        float effective_pct = invert_ ? (100.0f - pct) : pct;
        return min_us_ + static_cast<uint32_t>((effective_pct / 100.0f) * (max_us_ - min_us_));
    }

    void writeMicroseconds(uint32_t pulse_us) noexcept {
#ifdef ARDUINO
        // 50Hz = 20,000us period. 16-bit timer = 65535 ticks.
        // Ticks = pulse_us * 65535 / 20000
        uint32_t duty = (pulse_us * 65535ULL) / 20000ULL;
        ledcWrite(channel_, duty);
#else
        (void)pulse_us;
#endif
    }

    uint8_t pin_;
    uint8_t channel_;
    uint32_t min_us_;
    uint32_t max_us_;
    bool invert_;
    uint32_t idle_timeout_ms_;
    float current_position_pct_;
    uint32_t last_moved_ms_;
    bool is_attached_;
    bool begun_{false};
};

} // namespace SmokerController::Adapters::Actuators
