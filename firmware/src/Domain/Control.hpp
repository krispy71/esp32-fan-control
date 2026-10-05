#pragma once

#include "Configuration.hpp"
#include "Telemetry.hpp"

namespace SmokerController::Domain {

enum class ControlCommandKind : uint8_t {
    SetSetpoint, UpdateConfig, TriggerLidPause, CancelLidPause
};

struct ControlCommand {
    ControlCommandKind kind{ControlCommandKind::SetSetpoint};
    float setpoint_f{225.0f};
    SmokerConfig config{};
    uint32_t request_id{0};
    uint32_t expected_config_version{0};
    bool require_config_version{false};
};

enum class PersistenceStatus : uint8_t { NotConfigured, Unchanged, Saved, Failed };

struct ControlState {
    SmokerConfig config{};
    TelemetrySnapshot telemetry{};
    PersistenceStatus persistence{PersistenceStatus::NotConfigured};
    uint32_t last_command_id{0};
    bool last_command_accepted{false};
    uint32_t config_version{0};
};

} // namespace SmokerController::Domain
