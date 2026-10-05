#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <thread>
#include "../src/Adapters/Actuators/ESP32PWMBlowerAdapter.hpp"
#include "../src/Adapters/Actuators/ESP32ServoDamperAdapter.hpp"
#include "../src/Adapters/Sensors/MAX31855SensorAdapter.hpp"
#include "../src/Adapters/Sensors/MAX31856SensorAdapter.hpp"
#include "../src/Adapters/Display/InlandEInkAdapter.hpp"
#include "../src/Adapters/Storage/ESP32NVSConfigAdapter.hpp"
#include "../src/Adapters/Runtime/BoundedControlChannel.hpp"
#include "../src/Services/SmokerControlService.hpp"
using namespace SmokerController;

static void actuatorStartupAndIdle() {
    Adapters::Actuators::ESP32PWMBlowerAdapter blower;
    Adapters::Actuators::ESP32ServoDamperAdapter damper;
    Spy::clear(); Spy::now = 100;
    damper.configure({700, 2300, true});
    assert(Spy::events.empty());
    blower.begin(); damper.begin();
    assert(Spy::timerFrequency.at(0) == 25000);
    assert(Spy::timerFrequency.at(1) == 50);
    assert(Spy::duty.at(0) == 0);
    assert(Spy::duty.at(2) == 2300ULL * 65535 / 20000);
    assert(Spy::count("pwm", 2) == 1);
    damper.update(1599); assert(damper.isAttached());
    damper.update(1600); assert(!damper.isAttached());
    Spy::clear(); damper.setPosition(0); assert(Spy::events.empty());
    Spy::now = 2000; damper.setPosition(100);
    assert(Spy::count("attach", 26) == 1);
    assert(Spy::duty.at(2) == 700ULL * 65535 / 20000);
    damper.configure({800, 2100, false});
    assert(Spy::duty.at(2) == 2100ULL * 65535 / 20000);
    damper.configure({2500, 500, false});
    assert(Spy::duty.at(2) == 2100ULL * 65535 / 20000);
    blower.setSpeed(100); assert(Spy::duty.at(0) == 255);
    blower.setSpeed(NAN); assert(Spy::duty.at(0) == 0);
}
static void frame55(uint32_t raw) {
    Spy::response = {static_cast<uint8_t>(raw >> 24), static_cast<uint8_t>(raw >> 16), static_cast<uint8_t>(raw >> 8), static_cast<uint8_t>(raw)};
}
static void spiFramesAndFaults() {
    Adapters::Hardware::SharedSpiBus bus; assert(bus.begin());
    Adapters::Sensors::MAX31855SensorAdapter sensor(bus);
    sensor.begin(); Spy::now = 1234;
    frame55(400U << 18); auto value = sensor.readTemperature(Domain::SensorRole::Pit);
    assert(value.celsius == 100 && value.timestamp_ms == 1234 && value.isValid());
    frame55(0xFFFC0000); value = sensor.readTemperature(Domain::SensorRole::Pit);
    assert(value.celsius == -0.25f);
    for (const auto& pair : {std::pair<uint32_t, Domain::SensorFault>{1, Domain::SensorFault::Disconnected}, {2, Domain::SensorFault::ShortToGnd}, {4, Domain::SensorFault::ShortToVcc}}) {
        frame55(0x10000 | pair.first); value = sensor.readTemperature(Domain::SensorRole::Pit);
        assert(value.fault == pair.second && !value.isValid());
    }
    Spy::clear(); value = sensor.readTemperature(Domain::SensorRole::Food1);
    assert(value.fault == Domain::SensorFault::Disconnected && Spy::events.empty());
    value = sensor.readTemperature(Domain::SensorRole::Food2);
    assert(value.fault == Domain::SensorFault::Disconnected && Spy::events.empty());
    Adapters::Display::InlandEInkAdapter display(bus); display.begin();
    Domain::DisplayView view{};
    display.render(view, true);
    Spy::clear(); display.render(view, true);
    assert(Spy::count("gpio", 16) == 2); // hardware wake/reset before second RAM transfer
    Spy::response.clear(); Spy::yieldTransfer = true;
    std::thread reader([&] { for (int n = 0; n < 100; ++n) sensor.readTemperature(Domain::SensorRole::Pit); });
    for (int n = 0; n < 5; ++n) display.render(view, true);
    reader.join(); Spy::yieldTransfer = false;
    assert(Spy::activeCs == -1); // Spies assert lock/transaction/CS at every bus operation.
}
static void max56() {
    Adapters::Hardware::SharedSpiBus bus; assert(bus.begin());
    Adapters::Sensors::MAX31856SensorAdapter sensor(bus);
    Spy::clear(); Spy::now = 0; Spy::response = {0,0,0,0,0,0,0,0,0,0x90,0x03}; sensor.begin();
    std::vector<uint32_t> written;
    for (const auto& e : Spy::events) if (e.operation == "transfer") written.push_back(e.b);
    assert((written == std::vector<uint32_t>{0x80,0,0x81,3,0x82,0,0x80,0x90,0,0,0}));
    assert(sensor.readTemperature(Domain::SensorRole::Pit).fault == Domain::SensorFault::Stale);
    Spy::now = 250; Spy::response = {0, 0x06, 0x40, 0, 0}; // 100 C: 12800 << 5
    auto reading = sensor.readTemperature(Domain::SensorRole::Pit);
    assert(reading.celsius == 100 && reading.isValid());
    Spy::response = {0, 0xFF, 0xD8, 0, 0}; // -2.5 C
    reading = sensor.readTemperature(Domain::SensorRole::Pit);
    assert(reading.celsius == -2.5f);
    for (uint8_t fault : {1, 2, 4, 8, 16, 32, 64, 128}) {
        Spy::response = {0, 0, 0, 0, fault};
        assert(!sensor.readTemperature(Domain::SensorRole::Pit).isValid());
    }
    assert(sensor.readTemperature(Domain::SensorRole::Food1).fault == Domain::SensorFault::Disconnected);
}
static void persistenceAndServiceStartup() {
    Adapters::Storage::ESP32NVSConfigAdapter storage;
    Domain::SmokerConfig cfg;
    cfg.setpoint_f = 275; cfg.pid_kp = 4; cfg.pid_ki = .05f; cfg.pid_kd = 8;
    cfg.airflow_threshold_pct = 45; cfg.lid_drop_threshold_deg = 22; cfg.lid_pause_duration_ms = 250000;
    cfg.servo_min_pulse_us = 600; cfg.servo_max_pulse_us = 2400; cfg.servo_inverted = true;
    cfg.meat_probe_mode = Domain::MeatProbeMode::MeaterBleDirect;
    std::strcpy(cfg.meater_mac_filter, "01:23:45:67:89:AB"); std::strcpy(cfg.meater_cloud_token, "test-value");
    assert(storage.saveConfig(cfg));
    Adapters::Storage::ESP32NVSConfigAdapter fresh;
    Domain::SmokerConfig loaded;
    assert(fresh.loadConfig(loaded));
    assert(loaded.setpoint_f == 275 && loaded.pid_kp == 4 && loaded.pid_ki == .05f && loaded.pid_kd == 8);
    assert(loaded.airflow_threshold_pct == 45 && loaded.lid_drop_threshold_deg == 22 && loaded.lid_pause_duration_ms == 250000);
    assert(loaded.servo_min_pulse_us == 600 && loaded.servo_max_pulse_us == 2400 && loaded.servo_inverted);
    assert(loaded.meat_probe_mode == cfg.meat_probe_mode);
    assert(std::strcmp(loaded.meater_mac_filter, cfg.meater_mac_filter) == 0 && std::strcmp(loaded.meater_cloud_token, cfg.meater_cloud_token) == 0);
    Spy::nvsWriteFailure = true; cfg.setpoint_f = 300; assert(!fresh.saveConfig(cfg)); Spy::nvsWriteFailure = false;
    assert(storage.loadConfig(loaded) && loaded.setpoint_f == 275);
    Spy::nvsOpenFailure = true; assert(!storage.loadConfig(loaded) && !storage.saveConfig(cfg)); Spy::nvsOpenFailure = false;
    Spy::nvsReadFailure = true; assert(!storage.loadConfig(loaded)); Spy::nvsReadFailure = false;
    Adapters::Hardware::SharedSpiBus bus;
    Adapters::Sensors::MAX31855SensorAdapter sensor(bus);
    Adapters::Actuators::ESP32PWMBlowerAdapter blower;
    Adapters::Actuators::ESP32ServoDamperAdapter damper;
    Adapters::Runtime::BoundedControlChannel channel;
    Spy::clear(); Spy::startupAllowed = false;
    Services::SmokerControlService control(sensor, damper, blower, nullptr, 225, Domain::ActuatorCoordinator{}, Domain::PIDConfig{}, &storage, &channel);
    Spy::startupAllowed = true; assert(Spy::events.empty());
    blower.begin(); control.initialize(); damper.begin(); bus.begin(); sensor.begin();
    assert(Spy::duty.at(2) == 2400ULL * 65535 / 20000); // persisted calibrated closure
    assert(control.config().setpoint_f == 275);
    frame55(0x10001); control.executeCycle(1000);
    assert(control.isFailSafe() && Spy::duty.at(0) == 0);
    Domain::ControlState state; assert(channel.snapshot(state));
    assert(!state.telemetry.is_pit_valid);
}
static void boundedChannel() {
    Adapters::Runtime::BoundedControlChannel channel;
    Domain::ControlState state; assert(!channel.snapshot(state));
    Domain::ControlCommand command;
    for (uint32_t n=0; n<channel.capacity; ++n) { command.request_id=n; assert(channel.submit(command)); }
    assert(!channel.submit(command));
    for (uint32_t n=0; n<channel.capacity; ++n) { assert(channel.receive(command)); assert(command.request_id==n); }
    assert(!channel.receive(command));
    std::thread writer([&] { for (uint32_t n=0; n<10000; ++n) { Domain::ControlState s; s.last_command_id=n; s.config.setpoint_f=static_cast<float>(n); channel.publish(s); } });
    for (int i=0; i<10000; ++i) { if (channel.snapshot(state)) assert(state.config.setpoint_f == static_cast<float>(state.last_command_id)); }
    writer.join();
}
int main() {
    actuatorStartupAndIdle(); spiFramesAndFaults(); max56(); persistenceAndServiceStartup(); boundedChannel();
    std::cout << "Arduino hardware spies: all scenarios passed\n";
}
