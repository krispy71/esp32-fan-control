#include <atomic>
#include <cassert>
#include <cstring>
#include <iostream>
#include <thread>
#include "../src/Adapters/Sensors/BLEProbeAdapter.hpp"
#include "../src/Adapters/Sensors/MeaterBleClientAdapter.hpp"
#include "../src/Adapters/Sensors/MeaterCloudAdapter.hpp"
using namespace SmokerController;
int main() {
    Adapters::Sensors::BLEProbeAdapter ble;
    Adapters::Sensors::MeaterBleClientAdapter direct;
    Adapters::Sensors::MeaterCloudAdapter cloud;
    // Exercise all three production cache publishers against the control reader.
    // GATT tip raw 1592 decodes to (raw + 8) / 16 = 100 C.
    const uint8_t gatt[] = {0x38, 0x06};
    const char* json = "{\"data\":{\"devices\":[{\"id\":\"probe\",\"temperature\":{\"internal\":100,\"ambient\":200},\"battery\":80}]}}";
    std::atomic<bool> done{false};
    std::thread publisher([&] {
        for (uint32_t n = 1; n <= 20000; ++n) {
            assert(direct.processGattPayload(gatt, sizeof(gatt), n));
            assert(cloud.processCloudJson(json, n));
            ble.setTargetMac(n % 2 ? "01:23:45:67:89:AB" : "FE:DC:BA:98:76:54");
        }
        done = true;
    });
    while (!done) {
        const auto directReading = direct.readTemperature(Domain::SensorRole::Food1);
        const auto cloudReading = cloud.readTemperature(Domain::SensorRole::Food1);
        if (directReading.fault == Domain::SensorFault::Ok) assert(directReading.celsius == 100 && std::strcmp(directReading.probe_name, "MEATER (Direct)") == 0);
        if (cloudReading.fault == Domain::SensorFault::Ok) assert(cloudReading.celsius == 100);
        const auto mac = ble.targetMac();
        assert(mac.empty() || mac == "01:23:45:67:89:AB" || mac == "FE:DC:BA:98:76:54");
    }
    publisher.join();
    std::cout << "Wireless cache copy/concurrency checks passed\n";
}
