/**
 * ESP32 Smoker Fan & Damper Controller — Main Application Entry Point
 */

#ifdef ARDUINO
#include <Arduino.h>
#endif

#include "../Adapters/Actuators/ESP32PWMBlowerAdapter.hpp"
#include "../Adapters/Actuators/ESP32ServoDamperAdapter.hpp"
#include "../Adapters/Sensors/MAX31855SensorAdapter.hpp"
#include "../Adapters/Sensors/BLEProbeAdapter.hpp"
#include "../Adapters/Sensors/MeaterBleClientAdapter.hpp"
#include "../Adapters/Sensors/MeaterCloudAdapter.hpp"
#include "../Adapters/Sensors/CompositeSensorAdapter.hpp"
#include "../Adapters/Telemetry/SerialTelemetryAdapter.hpp"
#include "../Adapters/Network/WebServerAdapter.hpp"
#include "../Adapters/Storage/ESP32NVSConfigAdapter.hpp"
#include "../Adapters/Display/InlandEInkAdapter.hpp"
#include "../Services/SmokerControlService.hpp"

using namespace SmokerController;

// Hardware Pin Configuration
static constexpr uint8_t PIN_BLOWER_PWM = 25; // 5V N-MOSFET gate
static constexpr uint8_t PIN_SERVO_PWM  = 26; // 5V Servo signal (RJ45 Pin 6)
static constexpr uint8_t PIN_SPI_SCK    = 18; // Shared SPI Clock (MAX31855 & E-Ink)
static constexpr uint8_t PIN_SPI_MISO   = 19; // MAX31855 SPI Data Out
static constexpr uint8_t PIN_SPI_MOSI   = 23; // Inland E-Ink SPI Data In (DIN)
static constexpr uint8_t PIN_CS_PIT     = 5;  // MAX31855 Chip Select (Pit Probe)
static constexpr uint8_t PIN_CS_FOOD1   = 21; // MAX31855 Chip Select (Food Probe 1)
static constexpr uint8_t PIN_EINK_CS    = 4;  // Inland E-Ink Chip Select
static constexpr uint8_t PIN_EINK_DC    = 22; // Inland E-Ink Data/Command Control
static constexpr uint8_t PIN_EINK_RST   = 16; // Inland E-Ink Reset
static constexpr uint8_t PIN_EINK_BUSY  = 17; // Inland E-Ink Busy Output

// Concrete Adapters
static Adapters::Actuators::ESP32PWMBlowerAdapter blowerAdapter(PIN_BLOWER_PWM, 0);
static Adapters::Actuators::ESP32ServoDamperAdapter damperAdapter(PIN_SERVO_PWM, 1);
static Adapters::Sensors::MAX31855SensorAdapter wiredSensorAdapter(PIN_CS_PIT, PIN_CS_FOOD1, PIN_SPI_SCK, PIN_SPI_MISO);
static Adapters::Display::InlandEInkAdapter einkAdapter(
    PIN_EINK_CS,
    PIN_EINK_DC,
    PIN_EINK_RST,
    PIN_EINK_BUSY,
    PIN_SPI_SCK,
    PIN_SPI_MOSI,
    Adapters::Display::EInkModel::Inland_2_13_Inch
);
static Adapters::Sensors::BLEProbeAdapter bleProbeAdapter(30000);
static Adapters::Sensors::MeaterBleClientAdapter meaterDirectAdapter(30000);
static Adapters::Sensors::MeaterCloudAdapter meaterCloudAdapter(60000, 20000);
static Adapters::Sensors::CompositeSensorAdapter compositeSensorAdapter(
    wiredSensorAdapter,
    &bleProbeAdapter,
    &meaterDirectAdapter,
    &meaterCloudAdapter
);
static Adapters::Telemetry::SerialTelemetryAdapter telemetryAdapter;
static Adapters::Storage::ESP32NVSConfigAdapter storageAdapter("smoker_cfg");

// Core Smoker Service
static Services::SmokerControlService controlService(
    compositeSensorAdapter,
    damperAdapter,
    blowerAdapter,
    &telemetryAdapter,
    225.0f, // Default initial setpoint
    Domain::ActuatorCoordinator{},
    Domain::PIDConfig{},
    &storageAdapter
);

// Web Server Adapter
static Adapters::Network::WebServerAdapter webServerAdapter(controlService, 80);

#ifdef ARDUINO

// FreeRTOS Task on Core 1: Deterministic 1Hz Sensor Sampling & Control Loop
void controlLoopTask(void* pvParameters) {
    (void)pvParameters;
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t frequency = pdMS_TO_TICKS(1000); // 1000ms loop period

    for (;;) {
        vTaskDelayUntil(&last_wake_time, frequency);
        controlService.executeCycle(millis());
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("\n=======================================================");
    Serial.println("  ESP32 Smoker Fan & Servo Damper Controller v0.1.0");
    Serial.println("  PocketSWE Architecture — Initializing Hardware Ports");
    Serial.println("=======================================================");

    // Initialize Hardware Adapters
    blowerAdapter.begin();
    damperAdapter.begin();
    wiredSensorAdapter.begin();
    einkAdapter.begin();
    bleProbeAdapter.begin();
    meaterDirectAdapter.begin();
    webServerAdapter.begin("SmokerController", "smoker123");

    // Sync composite meat probe mode with loaded configuration
    const auto& initial_cfg = controlService.config();
    compositeSensorAdapter.setMode(initial_cfg.meat_probe_mode);
    meaterDirectAdapter.setTargetMac(initial_cfg.meater_mac_filter);
    meaterDirectAdapter.setEnabled(initial_cfg.meat_probe_mode == Domain::MeatProbeMode::MeaterBleDirect);
    meaterCloudAdapter.setApiToken(initial_cfg.meater_cloud_token);
    meaterCloudAdapter.setEnabled(initial_cfg.meat_probe_mode == Domain::MeatProbeMode::MeaterCloud);

    Serial.println("[Init] Hardware and Network Adapters initialized successfully.");
    Serial.printf("[Init] Default target setpoint: %.1f F\n", controlService.setpoint());

    // Launch Deterministic Control Task on Core 1
    xTaskCreatePinnedToCore(
        controlLoopTask,
        "SmokerControlLoop",
        4096,
        nullptr,
        2,        // Priority
        nullptr,
        1         // Core 1 (dedicated to real-time control)
    );

    Serial.println("[Init] Real-time control loop running on FreeRTOS Core 1.");
}

void loop() {
    uint32_t now = millis();
    // Core 0 loop: Housekeeping, Web Requests, BLE/Cloud polling, Display & Servo Idle-Detach check
    webServerAdapter.update();
    bleProbeAdapter.update(now);
    meaterDirectAdapter.update(now);
    meaterCloudAdapter.update(now);
    damperAdapter.update(now);
    einkAdapter.update(now, Domain::DisplayView::fromTelemetry(controlService.lastTelemetry()));

    // Keep active sensor routing synced with live config
    const auto& cfg = controlService.config();
    compositeSensorAdapter.setMode(cfg.meat_probe_mode);
    meaterDirectAdapter.setEnabled(cfg.meat_probe_mode == Domain::MeatProbeMode::MeaterBleDirect);
    meaterCloudAdapter.setEnabled(cfg.meat_probe_mode == Domain::MeatProbeMode::MeaterCloud);
    delay(10);
}

#else

// Desktop simulation / native entry point
int main() {
    std::cout << "ESP32 Smoker Controller — Native Host Build\n";
    bleProbeAdapter.begin();
    meaterDirectAdapter.begin();
    einkAdapter.begin();
    webServerAdapter.begin();
    webServerAdapter.update();
    einkAdapter.update(1000, Domain::DisplayView::fromTelemetry(controlService.lastTelemetry()));
    std::cout << "Native Host initialization verified cleanly.\n";
    return 0;
}

#endif
