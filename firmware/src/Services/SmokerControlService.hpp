#pragma once

#include "../Domain/Airflow.hpp"
#include "../Domain/Configuration.hpp"
#include "../Domain/LidDetector.hpp"
#include "../Domain/PID.hpp"
#include "../Domain/Temperature.hpp"
#include "Ports/DamperActuatorPort.hpp"
#include "Ports/BlowerActuatorPort.hpp"
#include "Ports/ConfigStoragePort.hpp"
#include "Ports/TemperatureSensorPort.hpp"
#include "Ports/TelemetryPort.hpp"

namespace SmokerController::Services {

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
        Ports::IConfigStoragePort* storage = nullptr
    ) noexcept
        : sensor_(sensor),
          damper_(damper),
          blower_(blower),
          telemetry_(telemetry),
          storage_(storage),
          setpoint_f_(target_setpoint_f),
          coordinator_(coordinator),
          pid_(target_setpoint_f, pid_config),
          lid_detector_(),
          is_fail_safe_(false),
          status_("INITIALIZED"),
          last_snapshot_{0, 0.0f, 0.0f, target_setpoint_f, 0.0f, 0.0f, 0.0f, false, false, false, "INITIALIZED"}
    {
        config_.setpoint_f = target_setpoint_f;
        config_.pid_kp = pid_config.kp;
        config_.pid_ki = pid_config.ki;
        config_.pid_kd = pid_config.kd;
        config_.airflow_threshold_pct = coordinator.blowerThreshold();
        if (storage_) {
            Domain::SmokerConfig loaded{};
            if (storage_->loadConfig(loaded) && loaded.isValid()) {
                config_ = loaded;
                setpoint_f_ = config_.setpoint_f;
                pid_.setSetpoint(config_.setpoint_f);
                pid_.setConfig(Domain::PIDConfig{
                    config_.pid_kp,
                    config_.pid_ki,
                    config_.pid_kd
                });
                coordinator_ = Domain::ActuatorCoordinator(config_.airflow_threshold_pct);
                lid_detector_ = Domain::LidOpenDetector(Domain::LidDetectorConfig{
                    config_.lid_drop_threshold_deg,
                    30000,
                    config_.lid_pause_duration_ms
                });
            }
        }
    }

    [[nodiscard]] const Domain::SmokerConfig& config() const noexcept { return config_; }

    void updateConfig(const Domain::SmokerConfig& new_cfg) noexcept {
        if (!new_cfg.isValid()) return;
        config_ = new_cfg;
        setpoint_f_ = config_.setpoint_f;
        pid_.setSetpoint(config_.setpoint_f);
        pid_.setConfig(Domain::PIDConfig{
            config_.pid_kp,
            config_.pid_ki,
            config_.pid_kd
        });
        coordinator_ = Domain::ActuatorCoordinator(config_.airflow_threshold_pct);
        lid_detector_ = Domain::LidOpenDetector(Domain::LidDetectorConfig{
            config_.lid_drop_threshold_deg,
            30000,
            config_.lid_pause_duration_ms
        });
        if (storage_) {
            storage_->saveConfig(config_);
        }
    }

    [[nodiscard]] float setpoint() const noexcept { return setpoint_f_; }
    void setSetpoint(float deg_f) noexcept {
        config_.setpoint_f = deg_f;
        setpoint_f_ = deg_f;
        pid_.setSetpoint(deg_f);
        if (storage_) {
            storage_->saveConfig(config_);
        }
    }

    [[nodiscard]] bool isFailSafe() const noexcept { return is_fail_safe_; }
    [[nodiscard]] bool isLidOpen() const noexcept { return lid_detector_.isActive(); }
    [[nodiscard]] const Ports::TelemetrySnapshot& lastTelemetry() const noexcept { return last_snapshot_; }

    void triggerLidPause(uint32_t current_time_ms) noexcept {
        lid_detector_.trigger(current_time_ms);
    }

    void cancelLidPause() noexcept {
        lid_detector_.reset();
    }

    Ports::TelemetrySnapshot executeCycle(uint32_t current_time_ms) noexcept {
        // 1. Read Sensors
        auto pit_reading = sensor_.readTemperature(Domain::SensorRole::Pit);
        auto meat_reading = sensor_.readTemperature(Domain::SensorRole::Food1);

        float meat_f = meat_reading.isValid() ? meat_reading.fahrenheit() : 0.0f;

        // 2. Fail-Safe Sensor Verification
        if (!pit_reading.isValid()) {
            is_fail_safe_ = true;
            status_ = "FAULT: SENSOR_DISCONNECTED";
            damper_.setPosition(0.0f);
            blower_.setSpeed(0.0f);

            Ports::TelemetrySnapshot snapshot{
                current_time_ms,
                0.0f,
                meat_f,
                setpoint_f_,
                0.0f,
                0.0f,
                0.0f,
                false,
                meat_reading.isValid(),
                false,
                status_
            };
            last_snapshot_ = snapshot;
            if (telemetry_) {
                telemetry_->publish(snapshot);
            }
            return snapshot;
        }

        is_fail_safe_ = false;
        float pit_f = pit_reading.fahrenheit();

        // 3. Lid-Open Detection
        bool lid_open = lid_detector_.update(pit_f, current_time_ms);
        if (lid_open) {
            status_ = "LID_OPEN";
            auto targets = coordinator_.coordinate(Domain::AirflowDemand{0.0f});
            damper_.setPosition(targets.damper_position_pct);
            blower_.setSpeed(targets.blower_speed_pct);

            Ports::TelemetrySnapshot snapshot{
                current_time_ms,
                pit_f,
                meat_f,
                setpoint_f_,
                targets.damper_position_pct,
                targets.blower_speed_pct,
                0.0f,
                true,
                meat_reading.isValid(),
                true,
                status_
            };
            last_snapshot_ = snapshot;
            if (telemetry_) {
                telemetry_->publish(snapshot);
            }
            return snapshot;
        }

        // 4. Closed-Loop Regulation
        auto demand = pid_.compute(pit_f, current_time_ms);
        auto targets = coordinator_.coordinate(demand);

        // 5. Command Physical Ports
        damper_.setPosition(targets.damper_position_pct);
        blower_.setSpeed(targets.blower_speed_pct);

        status_ = "REGULATING";
        Ports::TelemetrySnapshot snapshot{
            current_time_ms,
            pit_f,
            meat_f,
            setpoint_f_,
            targets.damper_position_pct,
            targets.blower_speed_pct,
            demand.value_pct,
            true,
            meat_reading.isValid(),
            false,
            status_
        };
        last_snapshot_ = snapshot;

        if (telemetry_) {
            telemetry_->publish(snapshot);
        }

        return snapshot;
    }

private:
    Ports::ITemperatureSensorPort& sensor_;
    Ports::IDamperActuatorPort& damper_;
    Ports::IBlowerActuatorPort& blower_;
    Ports::ITelemetryPublisherPort* telemetry_;
    Ports::IConfigStoragePort* storage_;
    Domain::SmokerConfig config_;
    float setpoint_f_;
    Domain::ActuatorCoordinator coordinator_;
    Domain::PIDRegulator pid_;
    Domain::LidOpenDetector lid_detector_;
    bool is_fail_safe_;
    const char* status_;
    Ports::TelemetrySnapshot last_snapshot_;
};

} // namespace SmokerController::Services
