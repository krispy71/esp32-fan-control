#pragma once

namespace SmokerController::Services::Ports {

class IDamperActuatorPort {
public:
    virtual ~IDamperActuatorPort() = default;
    virtual void setPosition(float position_pct) = 0;
};


} // namespace SmokerController::Services::Ports
