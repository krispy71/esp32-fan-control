#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <thread>
#include "arduino_spies/Max31856Device.hpp"
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
    Spy::now = 0; sensor.begin();
    Spy::clear(); Spy::now = 199;
    assert(sensor.readTemperature(Domain::SensorRole::Pit).fault == Domain::SensorFault::Stale);
    assert(Spy::events.empty());
    Spy::now = 200;
    frame55(400U << 18); auto value = sensor.readTemperature(Domain::SensorRole::Pit);
    assert(value.celsius == 100 && value.timestamp_ms == 200 && value.isValid());
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
    Max31856Device device;
    Adapters::Hardware::SharedSpiBus bus; assert(bus.begin());
    Adapters::Sensors::MAX31856SensorAdapter sensor(bus, 5, 21);
    Spy::clear(); Spy::now = 0; sensor.begin();
    std::vector<uint32_t> written;
    for (const auto& e : Spy::events) if (e.operation == "transfer" && e.a == 5) written.push_back(e.b);
    assert((written == std::vector<uint32_t>{0x80,0,0x81,3,0x82,0,0x80,0x10,0,0,0,0x80,0x50}));
    assert(sensor.readTemperature(Domain::SensorRole::Pit).fault == Domain::SensorFault::Stale);
    Spy::now = 169; sensor.update();
    assert(sensor.readTemperature(Domain::SensorRole::Pit).fault == Domain::SensorFault::Stale);
    Spy::now = 170; sensor.update();
    auto reading = sensor.readTemperature(Domain::SensorRole::Pit);
    assert(reading.celsius == 100 && reading.isValid() && reading.timestamp_ms == 170);
    assert(sensor.readTemperature(Domain::SensorRole::Food1).role == Domain::SensorRole::Food1);
    assert(sensor.readTemperature(Domain::SensorRole::Food1).isValid());
    device.pit.rawTemperature = 0xFFD800; // -2.5 C
    Spy::now = 340; sensor.update();
    reading = sensor.readTemperature(Domain::SensorRole::Pit);
    assert(reading.celsius == -2.5f);
    for (uint8_t fault : {1, 2, 4, 8, 16, 32, 64, 128}) {
        device.pit.fault = fault;
        Spy::now += 170; sensor.update();
        assert(!sensor.readTemperature(Domain::SensorRole::Pit).isValid());
    }
    assert(sensor.readTemperature(Domain::SensorRole::Food2).fault == Domain::SensorFault::Disconnected);
    device.pit.fault = 0;
    Spy::now += 170; sensor.update();
    assert(sensor.readTemperature(Domain::SensorRole::Pit).isValid());
    device.pit.stuck = true;
    Spy::now += 170; sensor.update();
    assert(sensor.readTemperature(Domain::SensorRole::Pit).isValid());
    Spy::now += 80; sensor.update();
    assert(sensor.readTemperature(Domain::SensorRole::Pit).fault == Domain::SensorFault::Stale);
    assert(sensor.readTemperature(Domain::SensorRole::Food1).isValid());
    Spy::now += 501; // Even missed upkeep cannot indefinitely preserve healthy data.
    assert(sensor.readTemperature(Domain::SensorRole::Food1).fault == Domain::SensorFault::Stale);
}
static void max56ConfigurationFailure() {
    for (int bad_register : {0, 1}) {
        Max31856Device device;
        if (bad_register == 0) device.pit.cr0Readback = 0;
        else device.pit.cr1Readback = 0; // B type instead of requested K type.
        Adapters::Hardware::SharedSpiBus bus; assert(bus.begin());
        Adapters::Sensors::MAX31856SensorAdapter sensor(bus);
        Adapters::Actuators::ESP32PWMBlowerAdapter blower;
        Adapters::Actuators::ESP32ServoDamperAdapter damper;
        Services::SmokerControlService control(sensor, damper, blower);
        Spy::now = 0; Spy::clear();
        blower.begin(); control.initialize(); damper.begin(); sensor.begin();
        assert(sensor.readTemperature(Domain::SensorRole::Pit).fault == Domain::SensorFault::Disconnected);
        Spy::now = 1000; sensor.update();
        const auto state = control.executeCycle(1000);
        assert(!state.is_pit_valid && state.demand_pct == 0);
        assert(Spy::duty.at(0) == 0 && state.damper_position_pct == 0);
        assert(Spy::duty.at(2) == 1000ULL * 65535 / 20000);
    }
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

struct RevisionSensor : Services::Ports::ITemperatureSensorPort {
    Domain::TemperatureReading readTemperature(Domain::SensorRole role) override {
        return Domain::TemperatureReading::fromFahrenheit(190, role, 0);
    }
};
struct RevisionRuntime {
    RevisionSensor sensor;
    Adapters::Actuators::ESP32PWMBlowerAdapter blower;
    Adapters::Actuators::ESP32ServoDamperAdapter damper;
    Adapters::Runtime::BoundedControlChannel channel;
    Services::SmokerControlService service;
    explicit RevisionRuntime(Services::Ports::IConfigStoragePort& storage)
        : service(sensor, damper, blower, nullptr, 225, Domain::ActuatorCoordinator{},
                  Domain::PIDConfig{}, &storage, &channel) {
        blower.begin(); service.initialize(); damper.begin();
    }
    Domain::ControlState state() const {
        Domain::ControlState out; assert(channel.snapshot(out)); return out;
    }
    Domain::ControlState submit(uint32_t version, uint16_t minimum) {
        Domain::ControlCommand command;
        command.kind = Domain::ControlCommandKind::UpdateConfig;
        command.config = service.config();
        command.config.servo_min_pulse_us = minimum;
        command.expected_config_version = version;
        command.require_config_version = true;
        command.request_id = 1;
        assert(channel.submit(command));
        service.executeCycle(1000);
        return state();
    }
};
static void durableConfigurationRevisions() {
    for (bool fail_latest : {false, true}) {
        Adapters::Storage::ESP32NVSConfigAdapter storage(fail_latest ? "rev-fail" : "rev-ok");
        RevisionRuntime first(storage);
        const auto original = first.state();
        assert(original.config_version == 0);
        auto applied = first.submit(original.config_version, 700);
        assert(applied.last_command_accepted);
        Spy::nvsWriteFailure = fail_latest;
        auto latest = first.submit(applied.config_version, 800);
        assert(latest.last_command_accepted && latest.config.servo_min_pulse_us == 800);
        assert(latest.persistence == (fail_latest ? Domain::PersistenceStatus::Failed : Domain::PersistenceStatus::Saved));
        Spy::nvsWriteFailure = false;
        Adapters::Storage::ESP32NVSConfigAdapter fresh_storage(fail_latest ? "rev-fail" : "rev-ok");
        RevisionRuntime restarted(fresh_storage);
        assert(restarted.state().config_version > latest.config_version);
        assert(restarted.state().config.servo_min_pulse_us == (fail_latest ? 700 : 800));
        for (auto stale_version : {original.config_version, latest.config_version}) {
            assert(!restarted.submit(stale_version, 1000).last_command_accepted);
            Domain::SmokerConfig persisted;
            assert(fresh_storage.loadConfig(persisted));
            assert(persisted.servo_min_pulse_us == (fail_latest ? 700 : 800));
        }
        assert(restarted.submit(restarted.state().config_version, 900).last_command_accepted);
    }
    Adapters::Storage::ESP32NVSConfigAdapter storage("rev-retry");
    Domain::SmokerConfig cfg; cfg.next_config_version = 1024; cfg.servo_min_pulse_us = 700;
    assert(storage.saveConfig(cfg));
    Spy::nvsWriteFailure = true;
    RevisionRuntime blocked(storage);
    assert(blocked.state().persistence == Domain::PersistenceStatus::Failed);
    auto rejected = blocked.submit(blocked.state().config_version, 800);
    assert(!rejected.last_command_accepted && rejected.config.servo_min_pulse_us == 700);
    assert(rejected.persistence == Domain::PersistenceStatus::Failed);
    assert(std::strcmp(rejected.telemetry.status, "REGULATING") == 0);
    Spy::nvsWriteFailure = false;
    assert(blocked.submit(rejected.config_version, 800).last_command_accepted);
    assert(storage.loadConfig(cfg) && cfg.next_config_version > blocked.state().config_version);

    Adapters::Storage::ESP32NVSConfigAdapter exhausted("rev-exhausted");
    cfg.next_config_version = UINT32_MAX - 1;
    assert(exhausted.saveConfig(cfg));
    RevisionRuntime terminal(exhausted);
    rejected = terminal.submit(terminal.state().config_version, 900);
    assert(!rejected.last_command_accepted && rejected.persistence == Domain::PersistenceStatus::Failed);
    assert(rejected.config_version == UINT32_MAX - 1 && rejected.config.servo_min_pulse_us == 800);

    Adapters::Storage::ESP32NVSConfigAdapter blocks("rev-blocks");
    RevisionRuntime many(blocks);
    for (size_t n = 0; n < 1024; ++n) {
        const auto result = many.submit(many.state().config_version, n % 2 ? 700 : 800);
        assert(result.last_command_accepted);
    }
    assert(blocks.loadConfig(cfg) && cfg.next_config_version > many.state().config_version);
    RevisionRuntime next(blocks);
    assert(next.state().config_version > many.state().config_version);
}
static void revisionReadFailurePreservesCalibration() {
    Adapters::Storage::ESP32NVSConfigAdapter storage("rev-unread");
    Domain::SmokerConfig cfg; cfg.servo_min_pulse_us = 700; cfg.next_config_version = 4096;
    assert(storage.saveConfig(cfg));
    const auto previous = Spy::nvs.at("rev-unreadconfig_v2");
    Spy::nvsReadFailure = true;
    RevisionRuntime unread(storage);
    assert(unread.state().persistence == Domain::PersistenceStatus::Failed);
    assert(!unread.submit(unread.state().config_version, 900).last_command_accepted);
    assert(Spy::nvs.at("rev-unreadconfig_v2") == previous);
    Spy::nvsReadFailure = false;
    Adapters::Storage::ESP32NVSConfigAdapter fresh("rev-unread");
    RevisionRuntime recovered(fresh);
    assert(recovered.state().config.servo_min_pulse_us == 700);
    assert(recovered.state().config_version == 4096);
}
static void legacyRevisionMigration() {
    // Fixed legacy on-flash representation, independent of the new adapter type.
    struct LegacyV1 {
        uint32_t version{1};
        float setpoint{275}, kp{4}, ki{0.04f}, kd{12}, threshold{45}, lid_drop{20};
        uint32_t lid_ms{120000};
        uint16_t min_us{750}, max_us{2250};
        uint8_t inverted{1}, mode{2};
        char token[96]{"old-token"};
        char mac[18]{"00:11:22:33:44:55"};
    };
    static_assert(sizeof(LegacyV1) == 152);
    LegacyV1 legacy;
    Preferences prefs; assert(prefs.begin("rev-old", false));
    assert(prefs.putBytes("config_v1", &legacy, sizeof(legacy)) == sizeof(legacy)); prefs.end();
    Adapters::Storage::ESP32NVSConfigAdapter storage("rev-old");
    Domain::SmokerConfig cfg; assert(storage.loadConfig(cfg));
    assert(cfg.next_config_version == 0);
    RevisionRuntime migrated(storage);
    assert(storage.loadConfig(cfg) && cfg.next_config_version == 1024);
    assert(cfg.setpoint_f == 275 && cfg.servo_min_pulse_us == 750 && cfg.servo_max_pulse_us == 2250);
    assert(cfg.servo_inverted && cfg.pid_kp == 4 && cfg.pid_ki == 0.04f && cfg.pid_kd == 12);
    assert(cfg.airflow_threshold_pct == 45 && cfg.lid_drop_threshold_deg == 20 && cfg.lid_pause_duration_ms == 120000);
    assert(cfg.meat_probe_mode == Domain::MeatProbeMode::MeaterBleDirect);
    assert(std::strcmp(cfg.meater_cloud_token, "old-token") == 0);
    assert(std::strcmp(cfg.meater_mac_filter, "00:11:22:33:44:55") == 0);
    assert(Spy::nvs.count("rev-oldconfig_v2") == 1);
    RevisionRuntime reboot(storage);
    assert(reboot.state().config_version > migrated.state().config_version);
    assert(!reboot.submit(migrated.state().config_version, 1000).last_command_accepted);
}

int main() {
    actuatorStartupAndIdle(); spiFramesAndFaults(); max56(); max56ConfigurationFailure(); persistenceAndServiceStartup(); boundedChannel();
    durableConfigurationRevisions(); revisionReadFailurePreservesCalibration(); legacyRevisionMigration();
    std::cout << "Arduino hardware spies: all scenarios passed\n";
}
