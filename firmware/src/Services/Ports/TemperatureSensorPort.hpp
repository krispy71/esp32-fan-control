#pragma once

#include "../../Domain/Temperature.hpp"

namespace SmokerController::Services::Ports {

class ITemperatureSensorPort {
public:
    virtual ~ITemperatureSensorPort() = default;
    virtual Domain::TemperatureReading readTemperature(Domain::SensorRole role) = 0;
};

} // namespace SmokerController::Services::Ports
