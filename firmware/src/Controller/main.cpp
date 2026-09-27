/**
 * ESP32 Smoker Fan & Damper Controller — Main Application Entry Point
 */

#ifdef ARDUINO
#include <Arduino.h>
#endif

#include "../Adapters/Actuators/ESP32PWMBlowerAdapter.hpp"
#include "../Adapters/Actuators/ESP32ServoDamperAdapter.hpp"
#include "../Adapters/Sensors/MAX31855SensorAdapter.hpp"
#include "../Adapters/Telemetry/SerialTelemetryAdapter.hpp"
#include "../Adapters/Network/WebServerAdapter.hpp"
#include "../Adapters/Storage/ESP32NVSConfigAdapter.hpp"
#include "../Services/SmokerControlService.hpp"

using namespace SmokerController;

// Hardware Pin Configuration
static constexpr uint8_t PIN_BLOWER_PWM = 25; // 5V N-MOSFET gate
static constexpr uint8_t PIN_SERVO_PWM  = 26; // 5V Servo signal (RJ45 Pin 6)
static constexpr uint8_t PIN_SPI_SCK    = 18; // MAX31855 SPI Clock
static constexpr uint8_t PIN_SPI_MISO   = 19; // MAX31855 SPI Data Out
static constexpr uint8_t PIN_CS_PIT     = 5;  // MAX31855 Chip Select (Pit Probe)
static constexpr uint8_t PIN_CS_FOOD1   = 21; // MAX31855 Chip Select (Food Probe 1)

// Concrete Adapters
static Adapters::Actuators::ESP32PWMBlowerAdapter blowerAdapter(PIN_BLOWER_PWM, 0);
static Adapters::Actuators::ESP32ServoDamperAdapter damperAdapter(PIN_SERVO_PWM, 1);
static Adapters::Sensors::MAX31855SensorAdapter sensorAdapter(PIN_CS_PIT, PIN_CS_FOOD1, PIN_SPI_SCK, PIN_SPI_MISO);
static Adapters::Telemetry::SerialTelemetryAdapter telemetryAdapter;
static Adapters::Storage::ESP32NVSConfigAdapter storageAdapter("smoker_cfg");

// Core Smoker Service
static Services::SmokerControlService controlService(
    sensorAdapter,
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
    sensorAdapter.begin();
    webServerAdapter.begin("SmokerController", "smoker123");

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
    // Core 0 loop: Housekeeping, Web Requests & Servo Idle-Detach check
    webServerAdapter.update();
    damperAdapter.update(millis());
    delay(10);
}

#else

// Desktop simulation / native entry point
int main() {
    std::cout << "ESP32 Smoker Controller — Native Host Build\n";
    webServerAdapter.begin();
    webServerAdapter.update();
    std::cout << "Native Host initialization verified cleanly.\n";
    return 0;
}

#endif
