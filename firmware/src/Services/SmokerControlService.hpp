#pragma once

#include <cstring>
#include "../Domain/Airflow.hpp"
#include "../Domain/Configuration.hpp"
#include "../Domain/LidDetector.hpp"
#include "../Domain/PID.hpp"
#include "../Domain/Temperature.hpp"
#include "Ports/DamperActuatorPort.hpp"
#include "Ports/BlowerActuatorPort.hpp"
#include "Ports/ConfigStoragePort.hpp"
#include "Ports/ControlChannelPort.hpp"
#include "Ports/TemperatureSensorPort.hpp"
#include "Ports/TelemetryPort.hpp"

namespace SmokerController::Services {

// Mutable state and all methods belong exclusively to the control task. Network
// callers submit copied commands and read copied states through IControlChannel.
class SmokerControlService {
public:
    SmokerControlService(
        Ports::ITemperatureSensorPort& sensor,
        Ports::IDamperActuatorPort& damper,
        Ports::IBlowerActuatorPort& blower,
        Ports::ITelemetryPublisherPort* telemetry = nullptr,
        float target_setpoint_f = 225.0f,
        Domain::ActuatorCoordinator coordinator = Domain::ActuatorCoordinator{},
        Domain::PIDConfig pid_config = Domain::PIDConfig{},
        Ports::IConfigStoragePort* storage = nullptr,
        Ports::IControlChannel* channel = nullptr
    ) noexcept
        : sensor_(sensor), damper_(damper), blower_(blower), telemetry_(telemetry),
          storage_(storage), channel_(channel), coordinator_(coordinator),
          pid_(target_setpoint_f, pid_config) {
        config_.setpoint_f = target_setpoint_f;
        config_.pid_kp = pid_config.kp;
        config_.pid_ki = pid_config.ki;
        config_.pid_kd = pid_config.kd;
        config_.airflow_threshold_pct = coordinator.blowerThreshold();
        if (!config_.isValid()) config_ = Domain::SmokerConfig{};
        last_snapshot_.setpoint_f = config_.setpoint_f;
    }

    // Call after platform startup, before starting network/control tasks. No port
    // or storage operation happens during construction.
    void initialize() noexcept {
        if (initialized_) return;
        blower_.setSpeed(0.0f);
        if (storage_) {
            Domain::SmokerConfig loaded{};
            if (storage_->loadConfig(loaded) && loaded.isValid()) config_ = loaded;
            persistence_ = Domain::PersistenceStatus::Unchanged;
        }
        applyConfiguration();
        damper_.configure(config_.damperCalibration());
        damper_.setPosition(0.0f);
        initialized_ = true;
        last_snapshot_.setpoint_f = config_.setpoint_f;
        std::strcpy(last_snapshot_.status, "INITIALIZED");
        publishState();
    }

    [[nodiscard]] const Domain::SmokerConfig& config() const noexcept { return config_; }

    bool updateConfig(const Domain::SmokerConfig& new_cfg) noexcept {
        if (!initialized_ || !new_cfg.isValid()) return false;
        const auto old_calibration = config_.damperCalibration();
        config_ = new_cfg;
        applyConfiguration();
        const auto calibration = config_.damperCalibration();
        if (old_calibration.min_pulse_us != calibration.min_pulse_us ||
            old_calibration.max_pulse_us != calibration.max_pulse_us ||
            old_calibration.inverted != calibration.inverted) {
            // Recalibration may move the damper; remove forced airflow first.
            blower_.setSpeed(0.0f);
            damper_.configure(calibration);
            damper_.setPosition(0.0f);
            pid_.suspend();
        }
        ++config_version_;
        if (storage_) {
            persistence_ = storage_->saveConfig(config_)
                ? Domain::PersistenceStatus::Saved : Domain::PersistenceStatus::Failed;
        }
        return true;
    }

    [[nodiscard]] float setpoint() const noexcept { return config_.setpoint_f; }
    bool setSetpoint(float deg_f) noexcept {
        auto updated = config_;
        updated.setpoint_f = deg_f;
        return updateConfig(updated);
    }

    [[nodiscard]] bool isFailSafe() const noexcept { return is_fail_safe_; }
    [[nodiscard]] bool isLidOpen() const noexcept { return lid_detector_.isActive(); }
    [[nodiscard]] const Ports::TelemetrySnapshot& lastTelemetry() const noexcept { return last_snapshot_; }

    void triggerLidPause(uint32_t current_time_ms) noexcept {
        lid_detector_.trigger(current_time_ms);
        pid_.suspend();
    }
    void cancelLidPause() noexcept { lid_detector_.reset(); }

    // The owner task can inhibit unsafe airflow between normal PID cycles.
    // Healthy readings never advance PID state or consume commands here.
    void verifyPitSafety(uint32_t current_time_ms) noexcept {
        if (!initialized_ || is_fail_safe_) return;
        if (sensor_.readTemperature(Domain::SensorRole::Pit).isValid()) return;
        auto snapshot = last_snapshot_;
        snapshot.timestamp_ms = current_time_ms;
        applyPitFault(snapshot);
        publish(snapshot);
    }

    Ports::TelemetrySnapshot executeCycle(uint32_t current_time_ms) noexcept {
        Domain::TelemetrySnapshot snapshot{};
        snapshot.timestamp_ms = current_time_ms;
        if (!initialized_) {
            stopAirflow();
            return publish(snapshot);
        }
        consumeCommands(current_time_ms);
        snapshot.setpoint_f = config_.setpoint_f;
        const auto pit = sensor_.readTemperature(Domain::SensorRole::Pit);
        const auto meat = sensor_.readTemperature(Domain::SensorRole::Food1);
        snapshot.is_meat_valid = meat.isValid();
        snapshot.meat_temp_f = meat.isValid() ? meat.fahrenheit() : 0.0f;
        snapshot.is_meat_wireless = meat.is_wireless;
        snapshot.meat_battery_pct = meat.battery_pct;
        std::strncpy(snapshot.meat_probe_name, meat.probe_name, sizeof(snapshot.meat_probe_name) - 1);

        if (!pit.isValid()) {
            applyPitFault(snapshot);
            return publish(snapshot);
        }

        is_fail_safe_ = false;
        snapshot.pit_temp_f = pit.fahrenheit();
        snapshot.is_pit_valid = true;
        snapshot.lid_open = lid_detector_.update(snapshot.pit_temp_f, current_time_ms);
        if (snapshot.lid_open) {
            pid_.suspend();
            stopAirflow();
            std::strcpy(snapshot.status, "LID_OPEN");
            return publish(snapshot);
        }

        const auto demand = pid_.compute(snapshot.pit_temp_f, current_time_ms);
        const auto targets = coordinator_.coordinate(demand);
        // Turn forced air off before closing the damper. Open the damper before
        // starting forced air, even when a prior cycle ran at full output.
        if (targets.blower_speed_pct == 0.0f) blower_.setSpeed(0.0f);
        damper_.setPosition(targets.damper_position_pct);
        if (targets.blower_speed_pct > 0.0f) blower_.setSpeed(targets.blower_speed_pct);
        snapshot.damper_position_pct = targets.damper_position_pct;
        snapshot.blower_speed_pct = targets.blower_speed_pct;
        snapshot.demand_pct = demand.value_pct;
        std::strcpy(snapshot.status, "REGULATING");
        return publish(snapshot);
    }

private:
    void applyPitFault(Domain::TelemetrySnapshot& snapshot) noexcept {
        is_fail_safe_ = true;
        pid_.suspend();
        // Do not let pre-fault samples trigger a spurious lid event on recovery.
        if (!lid_detector_.isActive()) lid_detector_.reset();
        stopAirflow();
        snapshot.is_pit_valid = false;
        snapshot.pit_temp_f = 0.0f;
        snapshot.damper_position_pct = snapshot.blower_speed_pct = snapshot.demand_pct = 0.0f;
        std::strcpy(snapshot.status, "FAULT: SENSOR_INVALID");
    }
    void applyConfiguration() noexcept {
        pid_.setSetpoint(config_.setpoint_f);
        auto tuning = pid_.config();
        tuning.kp = config_.pid_kp;
        tuning.ki = config_.pid_ki;
        tuning.kd = config_.pid_kd;
        pid_.setConfig(tuning);
        coordinator_ = Domain::ActuatorCoordinator(config_.airflow_threshold_pct,
                                                  coordinator_.minBlowerSpeed());
        // A configuration update must never cancel an active safety pause.
        lid_detector_.setConfig({config_.lid_drop_threshold_deg, 30000, config_.lid_pause_duration_ms});
    }
    void stopAirflow() noexcept {
        blower_.setSpeed(0.0f);
        damper_.setPosition(0.0f);
    }
    void consumeCommands(uint32_t now) noexcept {
        if (!channel_) return;
        Domain::ControlCommand command{};
        // Bound work even if producers continuously replenish the channel.
        for (size_t n = 0; n < Ports::IControlChannel::capacity && channel_->receive(command); ++n) {
            last_command_id_ = command.request_id;
            last_command_accepted_ = false;
            const bool changes_config = command.kind == Domain::ControlCommandKind::SetSetpoint ||
                command.kind == Domain::ControlCommandKind::UpdateConfig;
            if (changes_config && command.require_config_version &&
                command.expected_config_version != config_version_) continue;
            switch (command.kind) {
                case Domain::ControlCommandKind::SetSetpoint:
                    last_command_accepted_ = setSetpoint(command.setpoint_f); break;
                case Domain::ControlCommandKind::UpdateConfig:
                    last_command_accepted_ = updateConfig(command.config); break;
                case Domain::ControlCommandKind::TriggerLidPause:
                    triggerLidPause(now); last_command_accepted_ = true; break;
                case Domain::ControlCommandKind::CancelLidPause:
                    cancelLidPause(); last_command_accepted_ = true; break;
                default: break;
            }
        }
    }
    void publishState() noexcept {
        if (channel_) channel_->publish({config_, last_snapshot_, persistence_,
            last_command_id_, last_command_accepted_, config_version_});
    }
    Domain::TelemetrySnapshot publish(const Domain::TelemetrySnapshot& snapshot) noexcept {
        last_snapshot_ = snapshot;
        publishState();
        if (telemetry_) telemetry_->publish(snapshot);
        return snapshot;
    }

    Ports::ITemperatureSensorPort& sensor_;
    Ports::IDamperActuatorPort& damper_;
    Ports::IBlowerActuatorPort& blower_;
    Ports::ITelemetryPublisherPort* telemetry_;
    Ports::IConfigStoragePort* storage_;
    Ports::IControlChannel* channel_;
    Domain::SmokerConfig config_{};
    Domain::ActuatorCoordinator coordinator_;
    Domain::PIDRegulator pid_;
    Domain::LidOpenDetector lid_detector_{};
    bool initialized_{false};
    bool is_fail_safe_{false};
    Domain::TelemetrySnapshot last_snapshot_{};
    Domain::PersistenceStatus persistence_{Domain::PersistenceStatus::NotConfigured};
    uint32_t last_command_id_{0};
    bool last_command_accepted_{false};
    uint32_t config_version_{0};
};

} // namespace SmokerController::Services
