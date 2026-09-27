#pragma once

namespace SmokerController::Services::Ports {

class IBlowerActuatorPort {
public:
    virtual ~IBlowerActuatorPort() = default;
    virtual void setSpeed(float speed_pct) = 0;
};

} // namespace SmokerController::Services::Ports
