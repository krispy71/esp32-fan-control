/** ESP32 composition root: construct ownership after Arduino startup. */
#ifdef ARDUINO
#include <Arduino.h>
#else
#include <iostream>
#endif
#include "../Adapters/Actuators/ESP32PWMBlowerAdapter.hpp"
#include "../Adapters/Actuators/ESP32ServoDamperAdapter.hpp"
#include "../Adapters/Hardware/SharedSpiBus.hpp"
#include "../Adapters/Runtime/BoundedControlChannel.hpp"
#include "../Adapters/Sensors/MAX31855SensorAdapter.hpp"
#include "../Adapters/Sensors/MAX31856SensorAdapter.hpp"
#include "../Adapters/Sensors/BLEProbeAdapter.hpp"
#include "../Adapters/Sensors/MeaterBleClientAdapter.hpp"
#include "../Adapters/Sensors/MeaterCloudAdapter.hpp"
#include "../Adapters/Sensors/CompositeSensorAdapter.hpp"
#include "../Adapters/Telemetry/SerialTelemetryAdapter.hpp"
#include "../Adapters/Network/WebServerAdapter.hpp"
#include "../Adapters/Storage/ESP32NVSConfigAdapter.hpp"
#include "../Adapters/Display/InlandEInkAdapter.hpp"
#include "../Services/SmokerControlService.hpp"

#ifndef SMOKER_FOOD_CS
#define SMOKER_FOOD_CS -1
#endif

using namespace SmokerController;

class Application {
public:
    bool initializeHardware() {
        blower.begin(); // Fan off before storage, display, or network work.
        control.initialize(); // Load persisted calibration before the first servo pulse.
        damper.begin();
        if (!spi.begin(18, 19, 23)) return false;
        // Deassert every chip before the first transaction on the shared bus.
#ifdef ARDUINO
        pinMode(4, OUTPUT);
        digitalWrite(4, HIGH);
#endif
        wired.begin();
        sensors.setMode(control.config().meat_probe_mode);
        return true;
    }
    void controlStep(uint32_t now) {
        sensors.setMode(control.config().meat_probe_mode);
        control.executeCycle(now);
        damper.update(now);
    }
    void initializeNetwork() {
        display.begin();
        passiveBle.begin();
        directBle.begin();
        web.begin();
    }
    void networkStep(uint32_t now) {
        Domain::ControlState state;
        if (channel.snapshot(state)) {
            passiveBle.setTargetMac(state.config.meater_mac_filter);
            directBle.setTargetMac(state.config.meater_mac_filter);
            directBle.setEnabled(state.config.meat_probe_mode == Domain::MeatProbeMode::MeaterBleDirect);
            cloud.setApiToken(state.config.meater_cloud_token);
            cloud.setEnabled(state.config.meat_probe_mode == Domain::MeatProbeMode::MeaterCloud);
            display.update(now, Domain::DisplayView::fromTelemetry(state.telemetry));
        }
        web.update();
        passiveBle.update(now);
        directBle.update(now);
        cloud.update(now);
    }
private:
    Adapters::Hardware::SharedSpiBus spi;
    Adapters::Actuators::ESP32PWMBlowerAdapter blower{25, 0};
    Adapters::Actuators::ESP32ServoDamperAdapter damper{26, 2};
#ifdef SMOKER_MAX31856
    Adapters::Sensors::MAX31856SensorAdapter wired{spi, 5, SMOKER_FOOD_CS};
#else
    Adapters::Sensors::MAX31855SensorAdapter wired{spi, 5, SMOKER_FOOD_CS};
#endif
    Adapters::Display::InlandEInkAdapter display{spi, 4, 22, 16, 17};
    Adapters::Sensors::BLEProbeAdapter passiveBle;
    Adapters::Sensors::MeaterBleClientAdapter directBle;
    Adapters::Sensors::MeaterCloudAdapter cloud;
    Adapters::Sensors::CompositeSensorAdapter sensors{wired, &passiveBle, &directBle, &cloud};
    Adapters::Telemetry::SerialTelemetryAdapter telemetry;
    Adapters::Storage::ESP32NVSConfigAdapter storage;
    Adapters::Runtime::BoundedControlChannel channel;
    Services::SmokerControlService control{ sensors, damper, blower, &telemetry, 225.0f,
        Domain::ActuatorCoordinator{}, Domain::PIDConfig{}, &storage, &channel };
    Adapters::Network::WebServerAdapter web{channel};
};

#ifdef ARDUINO
void controlLoopTask(void* context) {
    auto& app = *static_cast<Application*>(context);
    TickType_t lastWake = xTaskGetTickCount();
    for (;;) {
        app.controlStep(millis());
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(1000));
    }
}
void networkLoopTask(void* context) {
    auto& app = *static_cast<Application*>(context);
    app.initializeNetwork();
    for (;;) {
        app.networkStep(millis());
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
void setup() {
    static Application app; // This owner is first constructed after platform startup.
    Serial.begin(115200);
    if (!app.initializeHardware()) {
        Serial.println("[Init] Shared bus unavailable; control remains off.");
        return;
    }
    if (xTaskCreatePinnedToCore(controlLoopTask, "SmokerControl", 6144, &app, 2, nullptr, 1) != pdPASS) {
        Serial.println("[Init] Control task unavailable; control remains off.");
        return;
    }
    if (xTaskCreatePinnedToCore(networkLoopTask, "SmokerNetwork", 8192, &app, 1, nullptr, 0) != pdPASS)
        Serial.println("[Init] Network task unavailable; autonomous control continues.");
}
void loop() { delay(1000); }
#else
int main() {
    Application app;
    if (!app.initializeHardware()) return 1;
    app.controlStep(1000);
    app.initializeNetwork();
    app.networkStep(1000);
    std::cout << "Native Host initialization verified cleanly.\n";
    return 0;
}
#endif
