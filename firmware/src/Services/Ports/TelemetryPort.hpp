#pragma once

#include "../../Domain/Telemetry.hpp"

namespace SmokerController::Services::Ports {

using TelemetrySnapshot = Domain::TelemetrySnapshot;

class ITelemetryPublisherPort {
public:
    virtual ~ITelemetryPublisherPort() = default;
    virtual void publish(const TelemetrySnapshot& snapshot) = 0;
};

} // namespace SmokerController::Services::Ports
