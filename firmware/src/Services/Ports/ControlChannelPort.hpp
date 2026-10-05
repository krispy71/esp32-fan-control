#pragma once

#include <cstddef>
#include "../../Domain/Control.hpp"

namespace SmokerController::Services::Ports {

// A bounded, thread-safe handoff. Every operation copies its input/output; callers
// never share service-owned references. No operation waits for another task's work.
class IControlChannel {
public:
    static constexpr size_t capacity = 8;
    virtual ~IControlChannel() = default;
    // False means full/unavailable: caller must report rejection, never drop silently.
    virtual bool submit(const Domain::ControlCommand& command) = 0;
    // Single control-task consumer, FIFO. False means no command available.
    virtual bool receive(Domain::ControlCommand& command) = 0;
    virtual void publish(const Domain::ControlState& state) = 0;
    // False until the first state is published; out_state is a coherent copy.
    virtual bool snapshot(Domain::ControlState& out_state) const = 0;
};

} // namespace SmokerController::Services::Ports
