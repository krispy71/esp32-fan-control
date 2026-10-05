/**
 * Desktop C++17 Unit Tests for Smoker Controller Domain & Services
 */

#include <cassert>
#include <cmath>
#include <iostream>

#include "../src/Domain/Airflow.hpp"
#include "../src/Domain/LidDetector.hpp"
#include "../src/Domain/PID.hpp"
#include "../src/Domain/Temperature.hpp"
#include "../src/Adapters/Sensors/MAX31855SensorAdapter.hpp"
#include "../src/Adapters/Sensors/BLEProbeAdapter.hpp"
#include "../src/Adapters/Sensors/MeaterBleClientAdapter.hpp"
#include "../src/Adapters/Sensors/MeaterCloudAdapter.hpp"
#include "../src/Adapters/Sensors/CompositeSensorAdapter.hpp"
#include "../src/Adapters/Storage/ESP32NVSConfigAdapter.hpp"
#include "../src/Domain/DisplayView.hpp"
#include "../src/Adapters/Display/InlandEInkAdapter.hpp"
#include "../src/Services/SmokerControlService.hpp"

using namespace SmokerController;

// Helper assertion with message
#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAILED: " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return 1; \
        } \
    } while (0)

static int testTemperatureDomain() {
    // 212°F = 100°C
    auto r = Domain::TemperatureReading::fromFahrenheit(212.0f, Domain::SensorRole::Pit, 1000);
    TEST_ASSERT(std::abs(r.celsius - 100.0f) < 0.01f, "212F must equal 100C");
    TEST_ASSERT(std::abs(r.fahrenheit() - 212.0f) < 0.01f, "fahrenheit() must return 212F");
    TEST_ASSERT(r.isValid(), "Healthy reading must be valid");

    auto disc = Domain::TemperatureReading::fromFahrenheit(
        225.0f, Domain::SensorRole::Pit, 1000, Domain::SensorFault::Disconnected
    );
    TEST_ASSERT(!disc.isValid(), "Disconnected sensor reading must not be valid");

    std::cout << "  [PASS] testTemperatureDomain\n";
    return 0;
}

static int testAirflowCoordinator() {
    Domain::ActuatorCoordinator coord(40.0f, 10.0f);

    // 0% demand -> Both 0% (anti-chimney draft)
    auto t0 = coord.coordinate(Domain::AirflowDemand{0.0f});
    TEST_ASSERT(t0.damper_position_pct == 0.0f, "0% demand must close damper");
    TEST_ASSERT(t0.blower_speed_pct == 0.0f, "0% demand must shut blower");

    // 20% demand -> Damper 50%, Blower 0% (natural draft)
    auto t20 = coord.coordinate(Domain::AirflowDemand{20.0f});
    TEST_ASSERT(std::abs(t20.damper_position_pct - 50.0f) < 0.1f, "20% demand must open damper 50%");
    TEST_ASSERT(t20.blower_speed_pct == 0.0f, "Blower must be off in natural draft stage");

    // 40% demand (at threshold) -> Damper 100%, Blower 0%
    auto t40 = coord.coordinate(Domain::AirflowDemand{40.0f});
    TEST_ASSERT(t40.damper_position_pct == 100.0f, "40% demand must open damper 100%");
    TEST_ASSERT(t40.blower_speed_pct == 0.0f, "Blower must remain off at threshold");

    // 70% demand -> Damper 100%, Blower 55%
    auto t70 = coord.coordinate(Domain::AirflowDemand{70.0f});
    TEST_ASSERT(t70.damper_position_pct == 100.0f, "Damper must be 100% in forced stage");
    TEST_ASSERT(std::abs(t70.blower_speed_pct - 55.0f) < 0.1f, "70% demand must run blower at 55%");

    // 100% demand -> Both 100%
    auto t100 = coord.coordinate(Domain::AirflowDemand{100.0f});
    TEST_ASSERT(t100.damper_position_pct == 100.0f, "Damper 100% at max demand");
    TEST_ASSERT(t100.blower_speed_pct == 100.0f, "Blower 100% at max demand");

    std::cout << "  [PASS] testAirflowCoordinator\n";
    return 0;
}

static int testPIDRegulator() {
    Domain::PIDConfig cfg;
    cfg.kp = 2.0f;
    cfg.ki = 0.1f;
    cfg.kd = 0.0f;
    cfg.integral_min = 0.0f;
    cfg.integral_max = 50.0f;

    Domain::PIDRegulator pid(225.0f, cfg);

    // Initial tick at 215°F (10° error) -> P term = 10 * 2 = 20%
    auto out1 = pid.compute(215.0f, 1000);
    TEST_ASSERT(std::abs(out1.value_pct - 20.0f) < 0.1f, "P term must match error * kp");

    // Hold cold for a long time -> Anti-windup must clamp integral at 50
    pid.compute(200.0f, 2000);
    pid.compute(200.0f, 50000);
    TEST_ASSERT(pid.integral() == 50.0f, "Integral must be clamped by anti-windup");

    pid.reset();
    TEST_ASSERT(pid.integral() == 0.0f, "Reset must clear integral");

    std::cout << "  [PASS] testPIDRegulator\n";
    return 0;
}

static int testLidDetector() {
    Domain::LidDetectorConfig cfg;
    cfg.drop_threshold_deg = 15.0f;
    cfg.time_window_ms = 30000;
    cfg.pause_duration_ms = 180000;

    Domain::LidOpenDetector detector(cfg);

    TEST_ASSERT(!detector.update(225.0f, 0), "Steady temp must not trigger lid detector");
    TEST_ASSERT(!detector.update(224.0f, 5000), "Small drop must not trigger lid detector");

    // Sudden 20° drop in 10s
    bool triggered = detector.update(204.0f, 10000);
    TEST_ASSERT(triggered, "20 deg drop in 10s must trigger lid open");
    TEST_ASSERT(detector.isActive(), "Detector must remain active");

    // Stays active during suppression window
    TEST_ASSERT(detector.update(205.0f, 60000), "Must stay active during pause window");

    // Exceeds pause duration (10s + 180s = 190s -> 195s)
    TEST_ASSERT(!detector.update(210.0f, 195000), "Must expire after pause duration");
    TEST_ASSERT(!detector.isActive(), "Detector must be inactive after expiry");

    std::cout << "  [PASS] testLidDetector\n";
    return 0;
}

static int testMAX31855Decoding() {
    // Simulated raw 32-bit register from MAX31855
    // 25.0°C = 100 raw ticks (100 * 0.25 = 25.0), shifted left by 18 = 0x01900000
    uint32_t raw_normal = (100 << 18);
    auto r = Adapters::Sensors::MAX31855SensorAdapter::decodeRawReading(raw_normal, Domain::SensorRole::Pit, 100);
    TEST_ASSERT(r.isValid(), "Normal reading must be valid");
    TEST_ASSERT(std::abs(r.celsius - 25.0f) < 0.01f, "Decoded Celsius must equal 25.0C");

    // Fault: Open Circuit (Bit 16 = Fault, Bit 0 = Open)
    uint32_t raw_open = 0x00010001;
    auto r_open = Adapters::Sensors::MAX31855SensorAdapter::decodeRawReading(raw_open, Domain::SensorRole::Pit, 200);
    TEST_ASSERT(!r_open.isValid(), "Open circuit must be marked invalid");
    TEST_ASSERT(r_open.fault == Domain::SensorFault::Disconnected, "Fault must be Disconnected");

    std::cout << "  [PASS] testMAX31855Decoding\n";
    return 0;
}

// Mock test doubles for Service integration test
class MockSensor : public Services::Ports::ITemperatureSensorPort {
public:
    Domain::TemperatureReading reading{0.0f, Domain::SensorRole::Pit, 0, Domain::SensorFault::Ok};
    Domain::TemperatureReading readTemperature(Domain::SensorRole role) override {
        (void)role;
        return reading;
    }
};

class MockDamper : public Services::Ports::IDamperActuatorPort {
public:
    float position{0.0f};
    void configure(const Domain::DamperCalibration&) override {}
    void setPosition(float p) override { position = p; }
};

class MockBlower : public Services::Ports::IBlowerActuatorPort {
public:
    float speed{0.0f};
    void setSpeed(float s) override { speed = s; }
};

static int testSmokerControlService() {
    MockSensor sensor;
    MockDamper damper;
    MockBlower blower;

    sensor.reading = Domain::TemperatureReading::fromFahrenheit(225.0f, Domain::SensorRole::Pit, 1000);
    Services::SmokerControlService service(sensor, damper, blower, nullptr, 225.0f);

    service.initialize();

    // Initial cycle at steady setpoint
    auto s1 = service.executeCycle(1000);
    TEST_ASSERT(!service.isFailSafe(), "Service must not be fail-safe initially");
    TEST_ASSERT(s1.pit_temp_f == 225.0f, "Pit temp should match reading");
    TEST_ASSERT(service.lastTelemetry().pit_temp_f == 225.0f, "lastTelemetry should match s1");

    // Test setpoint update
    service.setSetpoint(275.0f);
    TEST_ASSERT(service.setpoint() == 275.0f, "setpoint should be updated to 275");

    // Test manual lid pause trigger
    service.triggerLidPause(1500);
    TEST_ASSERT(service.isLidOpen(), "isLidOpen should be true after triggerLidPause");
    auto s_lid = service.executeCycle(1500);
    TEST_ASSERT(s_lid.lid_open, "Cycle should report lid_open true");
    TEST_ASSERT(damper.position == 0.0f, "Damper must be closed during lid pause");
    TEST_ASSERT(blower.speed == 0.0f, "Blower must be off during lid pause");

    service.cancelLidPause();
    TEST_ASSERT(!service.isLidOpen(), "isLidOpen should be false after cancelLidPause");

    // Simulate sensor disconnect
    sensor.reading = Domain::TemperatureReading::fromFahrenheit(
        225.0f, Domain::SensorRole::Pit, 2000, Domain::SensorFault::Disconnected
    );
    auto s_fault = service.executeCycle(2000);
    TEST_ASSERT(service.isFailSafe(), "Sensor disconnect must engage fail-safe");
    TEST_ASSERT(damper.position == 0.0f, "Fail-safe must close damper");
    TEST_ASSERT(blower.speed == 0.0f, "Fail-safe must shut blower");
    TEST_ASSERT(!s_fault.is_pit_valid, "Snapshot must record sensor failure");
    TEST_ASSERT(!service.lastTelemetry().is_pit_valid, "lastTelemetry must reflect sensor fault");

    std::cout << "  [PASS] testSmokerControlService\n";
    return 0;
}

static int testConfigStorage() {
    // 1. Invariant testing
    Domain::SmokerConfig valid_cfg{};
    TEST_ASSERT(valid_cfg.isValid(), "Default config must be valid");

    Domain::SmokerConfig invalid_sp{};
    invalid_sp.setpoint_f = 40.0f;
    TEST_ASSERT(!invalid_sp.isValid(), "Setpoint 40F must be invalid");

    Domain::SmokerConfig invalid_th{};
    invalid_th.airflow_threshold_pct = 95.0f;
    TEST_ASSERT(!invalid_th.isValid(), "Threshold 95% must be invalid");

    // 2. Storage adapter roundtrip
    Adapters::Storage::ESP32NVSConfigAdapter storage("test_cfg");
    Domain::SmokerConfig out_cfg{};
    TEST_ASSERT(!storage.loadConfig(out_cfg), "Empty storage should return false");

    valid_cfg.setpoint_f = 265.0f;
    valid_cfg.pid_kp = 4.2f;
    TEST_ASSERT(storage.saveConfig(valid_cfg), "Saving valid config must succeed");

    Domain::SmokerConfig loaded{};
    TEST_ASSERT(storage.loadConfig(loaded), "Loading saved config must succeed");
    TEST_ASSERT(loaded.setpoint_f == 265.0f, "Loaded setpoint must be 265.0");
    TEST_ASSERT(loaded.pid_kp == 4.2f, "Loaded kp must be 4.2");

    // 3. Service persistence integration
    MockSensor sensor;
    MockDamper damper;
    MockBlower blower;
    sensor.reading = Domain::TemperatureReading::fromFahrenheit(225.0f, Domain::SensorRole::Pit, 1000);

    Services::SmokerControlService service(
        sensor, damper, blower, nullptr, 225.0f,
        Domain::ActuatorCoordinator{}, Domain::PIDConfig{}, &storage
    );

    service.initialize();

    // Should have restored 265.0f from storage
    TEST_ASSERT(service.setpoint() == 265.0f, "Service must restore persisted setpoint 265.0 on init");

    // Updating setpoint via service should auto-save to storage
    service.setSetpoint(280.0f);
    Domain::SmokerConfig reloaded{};
    TEST_ASSERT(storage.loadConfig(reloaded), "Storage load must succeed");
    TEST_ASSERT(reloaded.setpoint_f == 280.0f, "Storage must reflect updated setpoint 280.0");

    std::cout << "  [PASS] testConfigStorage\n";
    return 0;
}

static int testBLEDecoder() {
    Domain::BLEProbeReading reading{};

    // 1. BTHome v2: 25.00°C (0x09C4), 88% battery
    const uint8_t bthome_payload[] = {0x40, 0x02, 0xC4, 0x09, 0x01, 0x58};
    TEST_ASSERT(
        Domain::BLEAdvertisementDecoder::decodeBTHomeV2(bthome_payload, sizeof(bthome_payload), reading),
        "BTHome v2 decode must succeed"
    );
    TEST_ASSERT(std::abs(reading.internal_temp_c - 25.0f) < 0.01f, "BTHome temp must be 25.0C");
    TEST_ASSERT(reading.battery_pct == 88, "BTHome battery must be 88%");
    TEST_ASSERT(reading.protocol == Domain::BLEProbeProtocol::BTHome, "Protocol must be BTHome");

    // 2. Inkbird: 55.4°C (0x022A = 554), 95% battery
    const uint8_t inkbird_payload[] = {0x00, 0x00, 0x2A, 0x02, 0x5F};
    reading = Domain::BLEProbeReading{};
    TEST_ASSERT(
        Domain::BLEAdvertisementDecoder::decodeInkbird(inkbird_payload, sizeof(inkbird_payload), reading),
        "Inkbird decode must succeed"
    );
    TEST_ASSERT(std::abs(reading.internal_temp_c - 55.4f) < 0.01f, "Inkbird temp must be 55.4C");
    TEST_ASSERT(reading.battery_pct == 95, "Inkbird battery must be 95%");
    TEST_ASSERT(reading.protocol == Domain::BLEProbeProtocol::Inkbird, "Protocol must be Inkbird");

    // 3. MEATER: tip 55.0°C (0x0226 = 550), ambient 110.0°C (0x044C = 1100), 90% battery
    const uint8_t meater_payload[] = {0x00, 0x00, 0x02, 0x26, 0x04, 0x4C, 0x5A};
    reading = Domain::BLEProbeReading{};
    TEST_ASSERT(
        Domain::BLEAdvertisementDecoder::decodeMeater(meater_payload, sizeof(meater_payload), reading),
        "MEATER decode must succeed"
    );
    TEST_ASSERT(std::abs(reading.internal_temp_c - 55.0f) < 0.01f, "MEATER tip temp must be 55.0C");
    TEST_ASSERT(std::abs(reading.ambient_temp_c - 110.0f) < 0.01f, "MEATER ambient temp must be 110.0C");
    TEST_ASSERT(reading.has_ambient, "MEATER must report ambient");
    TEST_ASSERT(reading.battery_pct == 90, "MEATER battery must be 90%");
    TEST_ASSERT(reading.protocol == Domain::BLEProbeProtocol::Meater, "Protocol must be Meater");

    // 4. SIG Environmental: 21.50°C (0x0866 = 2150)
    const uint8_t sig_payload[] = {0x66, 0x08};
    reading = Domain::BLEProbeReading{};
    TEST_ASSERT(
        Domain::BLEAdvertisementDecoder::decodeSigEnvironmental(sig_payload, sizeof(sig_payload), reading),
        "SIG Environmental decode must succeed"
    );
    TEST_ASSERT(std::abs(reading.internal_temp_c - 21.5f) < 0.01f, "SIG temp must be 21.5C");
    TEST_ASSERT(reading.protocol == Domain::BLEProbeProtocol::SigEnvironmental, "Protocol must be SIG");

    // 5. decodeAny auto-detection
    reading = Domain::BLEProbeReading{};
    TEST_ASSERT(
        Domain::BLEAdvertisementDecoder::decodeAny(bthome_payload, sizeof(bthome_payload), reading),
        "decodeAny must recognize BTHome"
    );
    TEST_ASSERT(reading.protocol == Domain::BLEProbeProtocol::BTHome, "Auto-detected protocol must be BTHome");

    std::cout << "  [PASS] testBLEDecoder\n";
    return 0;
}

static int testBLEProbeAdapter() {
    Adapters::Sensors::BLEProbeAdapter ble(30000);
    ble.begin();

    // Prior to packet arrival
    TEST_ASSERT(!ble.isConnected(1000), "Should not be connected before any packet");
    auto r_initial = ble.readTemperature(Domain::SensorRole::Food1);
    TEST_ASSERT(!r_initial.isValid(), "Initial reading must not be valid");

    // Simulate receiving MEATER packet
    const uint8_t meater_payload[] = {0x00, 0x00, 0x02, 0x26, 0x04, 0x4C, 0x5A};
    bool processed = ble.processAdvertisement(meater_payload, sizeof(meater_payload), "AA:BB:CC:DD:EE:FF", 5000);
    TEST_ASSERT(processed, "Process advertisement must succeed");
    TEST_ASSERT(ble.isConnected(6000), "Should be connected within timeout window");

    // Read Food1 (tip) and Food2 (ambient)
    auto r_food1 = ble.readTemperature(Domain::SensorRole::Food1);
    TEST_ASSERT(r_food1.isValid(), "Food1 reading must be valid");
    TEST_ASSERT(std::abs(r_food1.celsius - 55.0f) < 0.01f, "Food1 must be 55.0C");
    TEST_ASSERT(r_food1.is_wireless, "Food1 must be marked wireless");
    TEST_ASSERT(r_food1.battery_pct == 90, "Battery should be 90%");
    TEST_ASSERT(std::strcmp(r_food1.probe_name, "MEATER Probe") == 0, "Probe name should be MEATER Probe");

    auto r_food2 = ble.readTemperature(Domain::SensorRole::Food2);
    TEST_ASSERT(r_food2.isValid(), "Food2 reading must be valid");
    TEST_ASSERT(std::abs(r_food2.celsius - 110.0f) < 0.01f, "Food2 ambient must be 110.0C");

    // Test staleness timeout (last packet at 5000, now at 40000 -> 35s > 30s timeout)
    ble.setMockTime(40000);
    TEST_ASSERT(!ble.isConnected(40000), "Must be disconnected after staleness timeout");
    auto r_stale = ble.readTemperature(Domain::SensorRole::Food1);
    TEST_ASSERT(!r_stale.isValid(), "Stale reading must not be valid");
    TEST_ASSERT(r_stale.fault == Domain::SensorFault::Stale, "Fault must be Stale");

    // Test MAC filtering
    ble.setTargetMac("11:22:33:44:55:66");
    TEST_ASSERT(std::strcmp(ble.targetMac().c_str(), "11:22:33:44:55:66") == 0, "Target MAC should match");
    bool filtered = ble.processAdvertisement(meater_payload, sizeof(meater_payload), "AA:BB:CC:DD:EE:FF", 41000);
    TEST_ASSERT(!filtered, "Advertisement with different MAC must be rejected");

    bool matched = ble.processAdvertisement(meater_payload, sizeof(meater_payload), "11:22:33:44:55:66", 42000);
    TEST_ASSERT(matched, "Advertisement with matching MAC must be accepted");

    std::cout << "  [PASS] testBLEProbeAdapter\n";
    return 0;
}

static int testCompositeSensor() {
    MockSensor wired;
    Adapters::Sensors::BLEProbeAdapter ble(30000);
    Adapters::Sensors::CompositeSensorAdapter composite(wired, &ble);

    // Wired setup: Pit = 225°F, Food1 = 70°F
    wired.reading = Domain::TemperatureReading::fromFahrenheit(225.0f, Domain::SensorRole::Pit, 1000);

    // Pit is always wired
    auto pit_r = composite.readTemperature(Domain::SensorRole::Pit);
    TEST_ASSERT(pit_r.isValid(), "Pit reading must be valid");
    TEST_ASSERT(std::abs(pit_r.fahrenheit() - 225.0f) < 0.1f, "Pit must return wired reading 225F");
    TEST_ASSERT(!pit_r.is_wireless, "Pit must not be marked wireless");

    // BLE is not yet connected: Food1 falls back to wired
    auto food_wired = composite.readTemperature(Domain::SensorRole::Food1);
    TEST_ASSERT(food_wired.isValid(), "Food1 should fallback to wired sensor");
    TEST_ASSERT(!food_wired.is_wireless, "Fallback should not be wireless");

    // BLE receives wireless probe packet (tip = 57.2°C = 135.0°F)
    // 57.2°C = 572 raw in Inkbird format (0x023C = 572)
    const uint8_t inkbird_meat[] = {0x00, 0x00, 0x3C, 0x02, 0x55};
    ble.processAdvertisement(inkbird_meat, sizeof(inkbird_meat), "AA:BB:CC:DD:EE:FF", 2000);
    ble.setMockTime(2500);

    // Food1 now prefers the wireless probe
    auto food_ble = composite.readTemperature(Domain::SensorRole::Food1);
    TEST_ASSERT(food_ble.isValid(), "Wireless probe reading must be valid");
    TEST_ASSERT(food_ble.is_wireless, "Food1 must now be wireless");
    TEST_ASSERT(std::abs(food_ble.celsius - 57.2f) < 0.1f, "Food1 must read wireless 57.2C");
    TEST_ASSERT(food_ble.battery_pct == 85, "Food1 must report wireless battery 85%");

    // Pit still reads wired
    auto pit_r2 = composite.readTemperature(Domain::SensorRole::Pit);
    TEST_ASSERT(pit_r2.isValid() && !pit_r2.is_wireless, "Pit must remain wired even when BLE is active");

    // Wireless probe times out -> Food1 seamlessly falls back to wired
    ble.setMockTime(35000); // 33s since last packet > 30s timeout
    auto food_fallback = composite.readTemperature(Domain::SensorRole::Food1);
    TEST_ASSERT(food_fallback.isValid(), "Food1 must fallback when wireless times out");
    TEST_ASSERT(!food_fallback.is_wireless, "Fallback must return wired sensor");

    std::cout << "  [PASS] testCompositeSensor\n";
    return 0;
}

static int testMeaterGattAndCloudDecoding() {
    // 1. Test MEATER GATT Characteristic payload
    // Tip raw = 872 (0x0368 little endian) -> (872 + 8) / 16 = 55.0°C
    // ra = 1200, oa = 30 -> min_oa = 30 -> diff = 1170 -> amb_adj = (1170 * 16 * 589) / 1487 = 7413 -> amb = (872 + 7413 + 8) / 16 = 518.3°C
    // Let's use realistic ambient bytes:
    // ra = 200 (0x00C8), oa = 48 (0x0030) -> diff = 152 -> amb_adj = (152 * 16 * 589) / 1487 = 964 -> amb = (872 + 964 + 8) / 16 = 115.25°C
    const uint8_t meater_gatt[] = {
        0x68, 0x03, // tip = 872 (55.0°C)
        0xC8, 0x00, // ra = 200
        0x30, 0x00, // oa = 48
        0x00, 0x58  // battery = 88%
    };

    Domain::BLEProbeReading gatt_reading{};
    bool ok_gatt = Domain::BLEAdvertisementDecoder::decodeMeaterGatt(meater_gatt, sizeof(meater_gatt), gatt_reading);
    TEST_ASSERT(ok_gatt, "GATT decoder must decode valid MEATER payload");
    TEST_ASSERT(std::abs(gatt_reading.internal_temp_c - 55.0f) < 0.1f, "Tip temp must decode to 55.0C");
    TEST_ASSERT(std::abs(gatt_reading.internalFahrenheit() - 131.0f) < 0.2f, "131F internal temp");
    TEST_ASSERT(gatt_reading.has_ambient, "Ambient temp must be flagged present");
    TEST_ASSERT(std::abs(gatt_reading.ambient_temp_c - 115.25f) < 0.2f, "Ambient must decode correctly");
    TEST_ASSERT(gatt_reading.battery_pct == 88, "Battery must be 88%");
    TEST_ASSERT(std::string(gatt_reading.probe_name).find("MEATER") != std::string::npos, "Probe name must mention MEATER");

    // 2. Test MEATER Cloud JSON payload
    const char* cloud_json = "{\"data\":{\"devices\":[{\"id\":\"m_probe_1\",\"temperature\":{\"internal\":57.5,\"ambient\":110.0},\"battery\":95}]}}";
    Domain::BLEProbeReading cloud_reading{};
    bool ok_cloud = Domain::BLEAdvertisementDecoder::decodeMeaterCloudJson(cloud_json, cloud_reading);
    TEST_ASSERT(ok_cloud, "Cloud JSON decoder must decode valid MEATER Cloud payload");
    TEST_ASSERT(std::abs(cloud_reading.internal_temp_c - 57.5f) < 0.1f, "Internal must be 57.5C");
    TEST_ASSERT(cloud_reading.has_ambient, "Ambient must be present");
    TEST_ASSERT(std::abs(cloud_reading.ambient_temp_c - 110.0f) < 0.1f, "Ambient must be 110.0C");
    TEST_ASSERT(cloud_reading.battery_pct == 95, "Battery must be 95%");

    std::cout << "  [PASS] testMeaterGattAndCloudDecoding\n";
    return 0;
}

static int testMeaterAdaptersAndMultiModeRouting() {
    MockSensor wired;
    Adapters::Sensors::BLEProbeAdapter blePassive(30000);
    Adapters::Sensors::MeaterBleClientAdapter meaterDirect(30000);
    Adapters::Sensors::MeaterCloudAdapter meaterCloud(60000);

    Adapters::Sensors::CompositeSensorAdapter composite(
        wired,
        &blePassive,
        &meaterDirect,
        &meaterCloud
    );

    wired.reading = Domain::TemperatureReading::fromFahrenheit(225.0f, Domain::SensorRole::Pit, 1000);

    // Setup mock packets:
    // 1. Passive BLE: 50.0°C (122.0°F)
    const uint8_t inkbird[] = {0x00, 0x00, 0xF4, 0x01, 0x50}; // 500 = 50.0°C
    blePassive.processAdvertisement(inkbird, sizeof(inkbird), "11:22:33:44:55:66", 1000);
    blePassive.setMockTime(1500);

    // 2. Meater Direct: 55.0°C (131.0°F)
    const uint8_t meater_raw[] = {0x68, 0x03, 0xC8, 0x00, 0x30, 0x00, 0x00, 0x55};
    meaterDirect.processGattPayload(meater_raw, sizeof(meater_raw), 1000);
    meaterDirect.setMockTime(1500);

    // 3. Meater Cloud: 60.0°C (140.0°F)
    const char* cloud_json = "{\"internal\":60.0,\"ambient\":120.0,\"battery\":90}";
    meaterCloud.processCloudJson(cloud_json, 1000);
    meaterCloud.setMockTime(1500);

    // Test Mode 1: PassiveBle
    composite.setMode(Domain::MeatProbeMode::PassiveBle);
    auto r_passive = composite.readTemperature(Domain::SensorRole::Food1);
    TEST_ASSERT(std::abs(r_passive.celsius - 50.0f) < 0.1f, "Mode PassiveBle must return passive probe reading 50.0C");

    // Test Mode 2: MeaterBleDirect
    composite.setMode(Domain::MeatProbeMode::MeaterBleDirect);
    auto r_direct = composite.readTemperature(Domain::SensorRole::Food1);
    TEST_ASSERT(std::abs(r_direct.celsius - 55.0f) < 0.1f, "Mode MeaterBleDirect must return direct probe reading 55.0C");

    // Test Mode 3: MeaterCloud
    composite.setMode(Domain::MeatProbeMode::MeaterCloud);
    auto r_cloud = composite.readTemperature(Domain::SensorRole::Food1);
    TEST_ASSERT(std::abs(r_cloud.celsius - 60.0f) < 0.1f, "Mode MeaterCloud must return cloud reading 60.0C");

    // Test Mode 0: WiredOnly
    composite.setMode(Domain::MeatProbeMode::WiredOnly);
    auto r_wired = composite.readTemperature(Domain::SensorRole::Food1);
    TEST_ASSERT(!r_wired.is_wireless, "Mode WiredOnly must bypass all wireless adapters");

    // Test Disconnect / Fallback: If MeaterDirect times out, fallback to wired
    composite.setMode(Domain::MeatProbeMode::MeaterBleDirect);
    meaterDirect.setMockTime(40000); // Exceeded 30s timeout
    auto r_fb = composite.readTemperature(Domain::SensorRole::Food1);
    TEST_ASSERT(!r_fb.is_wireless, "Stale MeaterDirect must fallback to wired sensor");

    std::cout << "  [PASS] testMeaterAdaptersAndMultiModeRouting\n";
    return 0;
}

static int testDisplayViewAndInlandEInkAdapter() {
    // 1. Test Domain DisplayView conversion from TelemetrySnapshot
    Services::Ports::TelemetrySnapshot snap{
        12345, // timestamp_ms
        225.4f, // pit_temp_f
        165.2f, // meat_temp_f
        225.0f, // setpoint_f
        75.0f,  // damper_position_pct
        40.0f,  // blower_speed_pct
        55.0f,  // demand_pct
        true,   // is_pit_valid
        true,   // is_meat_valid
        false,  // lid_open
        "SMOKING",
        true,   // is_meat_wireless
        85,     // meat_battery_pct
        "MEATER_PLUS"
    };

    auto view = Domain::DisplayView::fromTelemetry(snap);
    TEST_ASSERT(std::abs(view.pit_temp_f - 225.4f) < 0.01f, "Pit temp should match telemetry");
    TEST_ASSERT(std::abs(view.meat_temp_f - 165.2f) < 0.01f, "Meat temp should match telemetry");
    TEST_ASSERT(std::abs(view.setpoint_f - 225.0f) < 0.01f, "Setpoint should match telemetry");
    TEST_ASSERT(view.is_meat_wireless == true, "Wireless meat flag should match");
    TEST_ASSERT(view.meat_battery_pct == 85, "Battery percentage should match");
    TEST_ASSERT(std::strcmp(view.meat_probe_name, "MEATER_PLUS") == 0, "Probe name should match");

    char pit_buf[32];
    view.formatPit(pit_buf, sizeof(pit_buf));
    TEST_ASSERT(std::strcmp(pit_buf, "225.4 F") == 0, "Formatted pit should be '225.4 F'");

    char meat_buf[32];
    view.formatMeat(meat_buf, sizeof(meat_buf));
    TEST_ASSERT(std::strcmp(meat_buf, "165.2 F") == 0, "Formatted meat should be '165.2 F'");

    // Test significant change detection
    auto v_small_drift = view;
    v_small_drift.pit_temp_f = 225.6f; // +0.2 deg F (< 0.5 threshold)
    TEST_ASSERT(!v_small_drift.hasSignificantChange(view), "Drift < 0.5F should not trigger significant change");

    auto v_big_drift = view;
    v_big_drift.pit_temp_f = 226.1f; // +0.7 deg F (>= 0.5 threshold)
    TEST_ASSERT(v_big_drift.hasSignificantChange(view), "Drift >= 0.5F must trigger significant change");

    auto v_lid = view;
    v_lid.lid_open = true;
    TEST_ASSERT(v_lid.hasSignificantChange(view), "Lid open transition must trigger significant change");

    auto v_fault = view;
    v_fault.pit_valid = false;
    TEST_ASSERT(v_fault.hasSignificantChange(view), "Sensor fault must trigger significant change");

    // 2. Test InlandEInkAdapter (2.13-inch model)
    Adapters::Hardware::SharedSpiBus spi_bus;
    Adapters::Display::InlandEInkAdapter adapter2_13(
        spi_bus, 4, 22, 16, 17,
        Adapters::Display::EInkModel::Inland_2_13_Inch
    );
    TEST_ASSERT(adapter2_13.width() == 250, "2.13 width must be 250");
    TEST_ASSERT(adapter2_13.height() == 122, "2.13 height must be 122");
    TEST_ASSERT(adapter2_13.bufferSize() == 32 * 122, "2.13 buffer size must be 3904 bytes");

    adapter2_13.begin();
    TEST_ASSERT(adapter2_13.refreshCount() == 0, "Initial refresh count should be 0");

    // Initial render
    adapter2_13.render(view, true);
    TEST_ASSERT(adapter2_13.refreshCount() == 1, "Refresh count should increment to 1 after render");

    // Verify drawing into framebuffer: header line at Y=15 should be black
    TEST_ASSERT(adapter2_13.getPixel(10, 15) == Adapters::Display::InlandEInkAdapter::COLOR_BLACK,
                "Header divider line pixel must be black");
    // Background pixel outside drawings should be white
    TEST_ASSERT(adapter2_13.getPixel(1, 1) == Adapters::Display::InlandEInkAdapter::COLOR_WHITE,
                "Corner background should be white");

    // 3. Test Cooldown and Update logic
    // Calling update at same timestamp (0 ms elapsed) should be rejected by cooldown
    adapter2_13.update(12345, v_big_drift);
    TEST_ASSERT(adapter2_13.refreshCount() == 1, "Immediate update must be suppressed by cooldown");

    // Calling update after 15 seconds with significant change should trigger refresh
    adapter2_13.update(12345 + 15000, v_big_drift);
    TEST_ASSERT(adapter2_13.refreshCount() == 2, "Update after cooldown with change must refresh");

    // Calling update after 5 seconds without change should be suppressed
    adapter2_13.update(12345 + 20000, v_big_drift);
    TEST_ASSERT(adapter2_13.refreshCount() == 2, "Unchanged update should be suppressed");

    // Calling update after 35 seconds (heartbeat) should trigger refresh
    adapter2_13.update(12345 + 15000 + 35000, v_big_drift);
    TEST_ASSERT(adapter2_13.refreshCount() == 3, "Heartbeat interval must trigger refresh");

    // 4. Test Telemetry Publisher interface
    snap.pit_temp_f = 230.0f;
    snap.timestamp_ms = 12345 + 15000 + 35000 + 15000;
    adapter2_13.publish(snap);
    TEST_ASSERT(adapter2_13.refreshCount() == 4, "Publishing telemetry after cooldown must refresh display");

    // 5. Test 1.54-inch model instantiation and dimensions
    Adapters::Display::InlandEInkAdapter adapter1_54(
        spi_bus, 4, 22, 16, 17,
        Adapters::Display::EInkModel::Inland_1_54_Inch
    );
    TEST_ASSERT(adapter1_54.width() == 200, "1.54 width must be 200");
    TEST_ASSERT(adapter1_54.height() == 200, "1.54 height must be 200");
    adapter1_54.begin();
    adapter1_54.render(view, true);
    TEST_ASSERT(adapter1_54.refreshCount() == 1, "1.54 adapter must render cleanly");

    std::cout << "  [PASS] testDisplayViewAndInlandEInkAdapter\n";
    return 0;
}

int main() {
    std::cout << "Running C++ Domain & Service Test Suite...\n";
    if (testTemperatureDomain() != 0) return 1;
    if (testAirflowCoordinator() != 0) return 1;
    if (testPIDRegulator() != 0) return 1;
    if (testLidDetector() != 0) return 1;
    if (testMAX31855Decoding() != 0) return 1;
    if (testSmokerControlService() != 0) return 1;
    if (testConfigStorage() != 0) return 1;
    if (testBLEDecoder() != 0) return 1;
    if (testBLEProbeAdapter() != 0) return 1;
    if (testCompositeSensor() != 0) return 1;
    if (testMeaterGattAndCloudDecoding() != 0) return 1;
    if (testMeaterAdaptersAndMultiModeRouting() != 0) return 1;
    if (testDisplayViewAndInlandEInkAdapter() != 0) return 1;

    std::cout << "\nALL 13 C++ TEST SUITES PASSED CLEANLY!\n";
    return 0;
}
