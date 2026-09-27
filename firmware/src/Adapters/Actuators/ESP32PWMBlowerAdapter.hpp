#pragma once

#include "../../Services/Ports/BlowerActuatorPort.hpp"
#include <algorithm>
#include <cstdint>

#ifdef ARDUINO
#include <Arduino.h>
#include <esp32-hal-ledc.h>
#endif

namespace SmokerController::Adapters::Actuators {

class ESP32PWMBlowerAdapter : public Services::Ports::IBlowerActuatorPort {
public:
    ESP32PWMBlowerAdapter(
        uint8_t gpio_pin = 25,
        uint8_t ledc_channel = 0,
        uint32_t frequency_hz = 25000, // 25kHz ultrasonic PWM (inaudible)
        uint8_t resolution_bits = 8
    ) noexcept
        : pin_(gpio_pin),
          channel_(ledc_channel),
          freq_hz_(frequency_hz),
          res_bits_(resolution_bits),
          max_duty_((1U << resolution_bits) - 1),
          current_speed_pct_(0.0f) {}

    void begin() noexcept {
#ifdef ARDUINO
        ledcSetup(channel_, freq_hz_, res_bits_);
        ledcAttachPin(pin_, channel_);
#endif
        setSpeed(0.0f);
    }

    void setSpeed(float speed_pct) noexcept override {
        current_speed_pct_ = std::clamp(speed_pct, 0.0f, 100.0f);
#ifdef ARDUINO
        uint32_t duty = static_cast<uint32_t>((current_speed_pct_ / 100.0f) * max_duty_);
        ledcWrite(channel_, duty);
#endif
    }

    [[nodiscard]] float currentSpeed() const noexcept { return current_speed_pct_; }

private:
    uint8_t pin_;
    uint8_t channel_;
    uint32_t freq_hz_;
    uint8_t res_bits_;
    uint32_t max_duty_;
    float current_speed_pct_;
};

} // namespace SmokerController::Adapters::Actuators
