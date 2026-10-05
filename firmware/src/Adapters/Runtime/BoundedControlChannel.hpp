#pragma once
#include "../../Services/Ports/ControlChannelPort.hpp"
#include "SnapshotMutex.hpp"
#include <array>

namespace SmokerController::Adapters::Runtime {
class BoundedControlChannel final : public Services::Ports::IControlChannel {
public:
    bool submit(const Domain::ControlCommand& command) noexcept override {
        SnapshotLock lock(mutex_);
        if (count_ == capacity) return false;
        commands_[(head_ + count_) % capacity] = command;
        ++count_;
        return true;
    }
    bool receive(Domain::ControlCommand& command) noexcept override {
        SnapshotLock lock(mutex_);
        if (count_ == 0) return false;
        command = commands_[head_];
        head_ = (head_ + 1) % capacity;
        --count_;
        return true;
    }
    void publish(const Domain::ControlState& state) noexcept override {
        SnapshotLock lock(mutex_);
        state_ = state;
        has_state_ = true;
    }
    bool snapshot(Domain::ControlState& state) const noexcept override {
        SnapshotLock lock(mutex_);
        if (!has_state_) return false;
        state = state_;
        return true;
    }
private:
    mutable SnapshotMutex mutex_;
    std::array<Domain::ControlCommand, capacity> commands_{};
    size_t head_{0};
    size_t count_{0};
    Domain::ControlState state_{};
    bool has_state_{false};
};
}
