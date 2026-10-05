#pragma once

#include "../../Domain/Configuration.hpp"

namespace SmokerController::Services::Ports {

class IDamperActuatorPort {
public:
    virtual ~IDamperActuatorPort() = default;
    virtual void configure(const Domain::DamperCalibration& calibration) = 0;
    virtual void setPosition(float position_pct) = 0;
};


} // namespace SmokerController::Services::Ports
