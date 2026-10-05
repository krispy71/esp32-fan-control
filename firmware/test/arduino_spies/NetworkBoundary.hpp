#pragma once
#include "Arduino.h"
#include "Services/Ports/ControlChannelPort.hpp"
namespace SmokerController::Adapters::Network {
// The transport is the sole replaced adapter in the startup composition harness.
// Firmware HTTPS receives separate build/security integration coverage.
class WebServerAdapter {
public:
    explicit WebServerAdapter(Services::Ports::IControlChannel& channel) : channel_(channel) {}
    void begin() { Spy::event("networkBegin"); }
    void update() {}
private:
    Services::Ports::IControlChannel& channel_;
};
}
