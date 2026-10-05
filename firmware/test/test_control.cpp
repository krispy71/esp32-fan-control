// Host regression driver for the production control service and domain contracts.
#include <cassert>
#include <cmath>
#include <cstring>
#include <deque>
#include <iostream>
#include <limits>
#include <string>
#include <vector>
#include "../src/Services/SmokerControlService.hpp"
#include "../src/Domain/DisplayView.hpp"
#include "../src/Adapters/Sensors/MAX31855SensorAdapter.hpp"

using namespace SmokerController;

struct Actuators : Services::Ports::IDamperActuatorPort, Services::Ports::IBlowerActuatorPort {
    float damper{-1.0f}, blower{-1.0f};
    Domain::DamperCalibration calibration{};
    std::vector<std::string> events;
    void configure(const Domain::DamperCalibration& value) override {
        assert(value.isValid()); calibration = value; events.push_back("configure");
    }
    void setPosition(float value) override {
        assert(value != 0.0f || blower <= 0.0f);
        damper = value; events.push_back(value == 0.0f ? "close" : "open");
    }
    void setSpeed(float value) override {
        assert(value <= 0.0f || damper > 0.0f);
        blower = value; events.push_back(value == 0.0f ? "off" : "on");
    }
};
struct Sensor : Services::Ports::ITemperatureSensorPort {
    Domain::TemperatureReading pit = Domain::TemperatureReading::fromFahrenheit(190, Domain::SensorRole::Pit, 0);
    Domain::TemperatureReading food{0, Domain::SensorRole::Food1, 0, Domain::SensorFault::Disconnected};
    unsigned calls{0};
    Domain::TemperatureReading readTemperature(Domain::SensorRole role) override {
        ++calls;
        return role == Domain::SensorRole::Pit ? pit : food;
    }
};
struct Storage : Services::Ports::IConfigStoragePort {
    Domain::SmokerConfig saved{};
    bool exists{false}, succeed{true};
    unsigned loads{0}, writes{0};
    bool loadConfig(Domain::SmokerConfig& out) override { ++loads; out = saved; return exists; }
    bool saveConfig(const Domain::SmokerConfig& value) override {
        ++writes; if (succeed) { saved = value; exists = true; } return succeed;
    }
};
struct Channel : Services::Ports::IControlChannel {
    std::deque<Domain::ControlCommand> commands;
    Domain::ControlState state{};
    bool published{false}, replenish{false};
    unsigned received{0};
    bool submit(const Domain::ControlCommand& command) override {
        if (commands.size() == capacity) return false;
        commands.push_back(command); return true;
    }
    bool receive(Domain::ControlCommand& command) override {
        if (commands.empty()) return false;
        command = commands.front(); commands.pop_front(); ++received;
        if (replenish) commands.push_back(command);
        return true;
    }
    void publish(const Domain::ControlState& value) override { state = value; published = true; }
    bool snapshot(Domain::ControlState& out) const override { out = state; return published; }
};
struct Publisher : Services::Ports::ITelemetryPublisherPort {
    Domain::TelemetrySnapshot saved{};
    void publish(const Domain::TelemetrySnapshot& value) override { saved = value; }
};

static void startupAndConfiguration() {
    Sensor sensor; Actuators actuators; Storage storage; Channel channel;
    storage.exists = true;
    storage.saved.setpoint_f = 265;
    storage.saved.servo_min_pulse_us = 800;
    storage.saved.servo_max_pulse_us = 2200;
    storage.saved.servo_inverted = true;
    storage.saved.meat_probe_mode = Domain::MeatProbeMode::WiredOnly;
    std::strcpy(storage.saved.meater_cloud_token, "test-placeholder");
    Services::SmokerControlService service(sensor, actuators, actuators, nullptr, 225,
        Domain::ActuatorCoordinator{}, Domain::PIDConfig{}, &storage, &channel);
    assert(storage.loads == 0 && actuators.events.empty());
    const auto preinit = service.executeCycle(1);
    assert(preinit.demand_pct == 0 && sensor.calls == 0);
    actuators.events.clear();
    service.initialize();
    assert(storage.loads == 1 && storage.writes == 0);
    assert(service.setpoint() == 265 && actuators.calibration.inverted);
    assert(actuators.calibration.min_pulse_us == 800 && actuators.calibration.max_pulse_us == 2200);
    assert((actuators.events == std::vector<std::string>{"off", "configure", "close"}));
    service.initialize();
    assert(storage.loads == 1 && actuators.events.size() == 3);
    assert(!service.setSetpoint(99));
    assert(!service.setSetpoint(std::numeric_limits<float>::quiet_NaN()));
    assert(storage.writes == 0 && service.setpoint() == 265);
    assert(service.setSetpoint(280));
    assert(storage.saved.servo_inverted && storage.saved.meat_probe_mode == Domain::MeatProbeMode::WiredOnly);
    assert(std::strcmp(storage.saved.meater_cloud_token, "test-placeholder") == 0);
    service.executeCycle(1000);
    assert(channel.state.persistence == Domain::PersistenceStatus::Saved);
    storage.succeed = false;
    assert(service.setSetpoint(290));
    service.executeCycle(2000);
    assert(channel.state.persistence == Domain::PersistenceStatus::Failed);
    assert(storage.saved.setpoint_f == 280 && service.setpoint() == 290);
    Sensor restarted_sensor; Actuators restarted_actuators;
    Services::SmokerControlService restarted(restarted_sensor, restarted_actuators, restarted_actuators,
        nullptr, 225, Domain::ActuatorCoordinator{}, Domain::PIDConfig{}, &storage);
    restarted.initialize();
    assert(restarted.setpoint() == 280 && restarted_actuators.calibration.inverted);

    for (const auto calibration : {Domain::DamperCalibration{499, 2000, false},
        Domain::DamperCalibration{1000, 2501, false}, Domain::DamperCalibration{2000, 2000, false},
        Domain::DamperCalibration{2100, 1000, true}}) {
        auto invalid = service.config();
        invalid.servo_min_pulse_us = calibration.min_pulse_us;
        invalid.servo_max_pulse_us = calibration.max_pulse_us;
        assert(!calibration.isValid() && !invalid.isValid() && !service.updateConfig(invalid));
    }
    Domain::SmokerConfig malformed{};
    std::memset(malformed.meater_cloud_token, 'x', sizeof(malformed.meater_cloud_token));
    assert(!malformed.isValid());
    auto changed = service.config(); changed.servo_min_pulse_us = 600;
    actuators.events.clear();
    assert(service.updateConfig(changed));
    assert((actuators.events == std::vector<std::string>{"off", "configure", "close"}));
}

static void activeOutputsThenFault() {
    for (auto fault : {Domain::SensorFault::Disconnected, Domain::SensorFault::ShortToGnd,
                      Domain::SensorFault::ShortToVcc, Domain::SensorFault::OutOfRange, Domain::SensorFault::Stale}) {
        Sensor sensor; Actuators actuators;
        Services::SmokerControlService service(sensor, actuators, actuators);
        service.initialize();
        auto healthy = service.executeCycle(0);
        assert(healthy.is_pit_valid && !healthy.is_meat_valid); // one wired pit probe suffices
        assert(actuators.blower > 0 && actuators.damper > 0);
        sensor.pit.fault = fault;
        auto failed = service.executeCycle(1000);
        assert(!failed.is_pit_valid && service.isFailSafe());
        assert(actuators.blower == 0 && actuators.damper == 0 && failed.demand_pct == 0);
    }
    for (float temp : {-40.25f, 500.25f, std::numeric_limits<float>::quiet_NaN(),
                       std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()}) {
        Sensor sensor; Actuators actuators;
        Services::SmokerControlService service(sensor, actuators, actuators);
        service.initialize(); service.executeCycle(0);
        assert(actuators.blower > 0 && actuators.damper > 0);
        sensor.pit.celsius = temp;
        assert(!sensor.pit.isValid());
        auto failed = service.executeCycle(1000);
        assert(!failed.is_pit_valid && actuators.blower == 0 && actuators.damper == 0);
    }
    for (float temp : {-40.0f, 500.0f}) {
        Domain::TemperatureReading boundary{temp}; assert(boundary.isValid());
    }
}

static void decodedTemperatureFrames() {
    using Decoder = Adapters::Sensors::MAX31855SensorAdapter;
    const auto negative = Decoder::decodeRawReading((static_cast<uint32_t>(-40) & 0x3fff) << 18,
        Domain::SensorRole::Pit, 45);
    assert(negative.celsius == -10.0f && negative.isValid() && negative.timestamp_ms == 45);
    const std::pair<uint32_t, Domain::SensorFault> faults[] = {
        {0x10001, Domain::SensorFault::Disconnected}, {0x10002, Domain::SensorFault::ShortToGnd},
        {0x10004, Domain::SensorFault::ShortToVcc}};
    for (const auto& entry : faults) {
        const auto reading = Decoder::decodeRawReading(entry.first, Domain::SensorRole::Pit, 0);
        assert(!reading.isValid() && reading.fault == entry.second);
        Sensor sensor; Actuators actuators;
        Services::SmokerControlService service(sensor, actuators, actuators);
        service.initialize(); service.executeCycle(0);
        assert(actuators.blower > 0);
        sensor.pit = reading; service.executeCycle(1000);
        assert(actuators.blower == 0 && actuators.damper == 0);
    }
}

static void suppressionRecovery() {
    Domain::PIDConfig tuning{}; tuning.kp = 0; tuning.ki = 0.1f; tuning.kd = 0;
    Domain::PIDRegulator pid(225, tuning);
    pid.compute(220, 0); pid.compute(220, 1000);
    const float integral = pid.integral(); assert(integral == 5);
    pid.suspend();
    assert(std::abs(pid.compute(200, 181000).value_pct - 0.5f) < 0.001f);
    assert(pid.integral() == integral);
    assert(std::abs(pid.compute(220, 182000).value_pct - 1.0f) < 0.001f);

    for (int pause : {0, 1, 2}) {
        Sensor sensor; Actuators actuators;
        sensor.pit = Domain::TemperatureReading::fromFahrenheit(220, Domain::SensorRole::Pit, 0);
        Services::SmokerControlService service(sensor, actuators, actuators, nullptr, 225,
            Domain::ActuatorCoordinator{}, tuning);
        service.initialize(); service.executeCycle(0);
        const auto before = service.executeCycle(1000); assert(before.demand_pct > 0);
        if (pause == 2) sensor.pit.fault = Domain::SensorFault::Disconnected;
        else if (pause == 1) sensor.pit = Domain::TemperatureReading::fromFahrenheit(190, Domain::SensorRole::Pit, 2000);
        else service.triggerLidPause(2000);
        const auto paused = service.executeCycle(2000); assert(paused.demand_pct == 0);
        sensor.pit = Domain::TemperatureReading::fromFahrenheit(220, Domain::SensorRole::Pit, 182000);
        const auto resumed = service.executeCycle(182000);
        assert(!resumed.lid_open && resumed.is_pit_valid);
        assert(std::abs(resumed.demand_pct - before.demand_pct) < 0.001f);
    }
    // A target update cannot cancel suppression; manual cancellation resumes without windup.
    Sensor sensor; Actuators actuators;
    Services::SmokerControlService service(sensor, actuators, actuators, nullptr, 225,
        Domain::ActuatorCoordinator{}, tuning);
    service.initialize(); service.executeCycle(0); service.executeCycle(1000);
    service.triggerLidPause(2000); service.setSetpoint(230);
    assert(service.executeCycle(3000).lid_open);
    service.cancelLidPause();
    assert(service.executeCycle(180000).demand_pct < 4.0f);

    Domain::PIDConfig saturated{}; saturated.kp = 10; saturated.ki = 1; saturated.kd = 0;
    Domain::PIDRegulator limited(225, saturated);
    limited.compute(200, 0); limited.compute(200, 1000);
    assert(limited.integral() == 0);
}

static void commandBoundaryAndOwnership() {
    Sensor sensor; Actuators actuators; Channel channel; Publisher publisher;
    sensor.food = Domain::TemperatureReading::fromFahrenheit(145, Domain::SensorRole::Food1, 0,
        Domain::SensorFault::Ok, true, 75, "owned probe name");
    Services::SmokerControlService service(sensor, actuators, actuators, &publisher, 225,
        Domain::ActuatorCoordinator{}, Domain::PIDConfig{}, nullptr, &channel);
    service.initialize();
    Domain::ControlCommand command{}; command.setpoint_f = 250; command.request_id = 10;
    command.require_config_version = true;
    assert(channel.submit(command));
    assert(service.setpoint() == 225); // enqueue cannot modify mutable control state
    auto copied = service.executeCycle(1000);
    assert(copied.setpoint_f == 250 && channel.state.config.setpoint_f == 250);
    assert(channel.state.config_version == 1 && channel.state.last_command_accepted);
    command.setpoint_f = 275; command.request_id = 11; // stale version zero
    assert(channel.submit(command)); service.executeCycle(2000);
    assert(service.setpoint() == 250 && !channel.state.last_command_accepted);
    assert(channel.state.last_command_id == 11);
    command.expected_config_version = 1; command.setpoint_f = 500;
    channel.submit(command); service.executeCycle(3000);
    assert(service.setpoint() == 250 && channel.state.config_version == 1);
    command.kind = Domain::ControlCommandKind::TriggerLidPause;
    channel.submit(command); assert(service.executeCycle(4000).lid_open);
    command.kind = Domain::ControlCommandKind::CancelLidPause;
    channel.submit(command); assert(!service.executeCycle(5000).lid_open);

    Domain::ControlState old_state{}; assert(channel.snapshot(old_state));
    std::memset(sensor.food.probe_name, 'z', sizeof(sensor.food.probe_name));
    service.executeCycle(6000);
    assert(std::strcmp(copied.meat_probe_name, "owned probe name") == 0);
    assert(std::strcmp(old_state.telemetry.meat_probe_name, "owned probe name") == 0);
    assert(std::strcmp(copied.status, "REGULATING") == 0);
    auto view = Domain::DisplayView::fromTelemetry(copied);
    assert(std::strcmp(view.meat_probe_name, "owned probe name") == 0);
    assert(std::strlen(publisher.saved.meat_probe_name) == 31);

    channel.replenish = true; command.require_config_version = false;
    channel.submit(command); const auto previous = channel.received;
    service.executeCycle(7000);
    assert(channel.received - previous == Services::Ports::IControlChannel::capacity);
    assert(sensor.calls == 14); // continual commands cannot starve sensor acquisition
}

int main() {
    startupAndConfiguration(); activeOutputsThenFault(); decodedTemperatureFrames();
    suppressionRecovery(); commandBoundaryAndOwnership();
    std::cout << "Core production regression suites passed\n";
}
