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

    // Initial cycle at steady setpoint
    auto s1 = service.executeCycle(1000);
    TEST_ASSERT(!service.isFailSafe(), "Service must not be fail-safe initially");
    TEST_ASSERT(s1.pit_temp_f == 225.0f, "Pit temp should match reading");

    // Simulate sensor disconnect
    sensor.reading = Domain::TemperatureReading::fromFahrenheit(
        225.0f, Domain::SensorRole::Pit, 2000, Domain::SensorFault::Disconnected
    );
    auto s_fault = service.executeCycle(2000);
    TEST_ASSERT(service.isFailSafe(), "Sensor disconnect must engage fail-safe");
    TEST_ASSERT(damper.position == 0.0f, "Fail-safe must close damper");
    TEST_ASSERT(blower.speed == 0.0f, "Fail-safe must shut blower");
    TEST_ASSERT(!s_fault.is_pit_valid, "Snapshot must record sensor failure");

    std::cout << "  [PASS] testSmokerControlService\n";
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

    std::cout << "\nALL 6 C++ TEST SUITES PASSED CLEANLY!\n";
    return 0;
}
