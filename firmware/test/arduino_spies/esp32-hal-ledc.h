#pragma once
#include "Arduino.h"
namespace Spy { inline std::map<int, int> timerFrequency; inline std::map<int, uint32_t> duty; }
inline double ledcSetup(int channel, double frequency, int resolution) {
    Spy::timerFrequency[channel / 2] = static_cast<int>(frequency);
    Spy::event("pwmSetup", channel, resolution);
    return frequency;
}
inline void ledcAttachPin(int pin, int channel) { Spy::event("attach", pin, channel); }
inline void ledcDetachPin(int pin) { Spy::event("detach", pin); }
inline void ledcWrite(int channel, uint32_t duty) { Spy::duty[channel] = duty; Spy::event("pwm", channel, duty); }
