#include "arduino_spies/Max31856Device.hpp"
#include STARTUP_ENTRYPOINT
#include <cassert>
#include <cstdlib>
#include <iostream>

int main(int argc, char** argv) {
    assert(argc == 2);
    const uint32_t disconnectAt = static_cast<uint32_t>(std::strtoul(argv[1], nullptr, 10));
    Max31856Device device;
    device.pit.rawTemperature = 0x04B000; // 75 C, enough demand to run the blower.
    device.pit.disconnectedAt = disconnectAt;
    Spy::now = 0;
    setup();
    assert(Spy::tasks.size() == 2);
    uint32_t faultObservedAt = 0;
    uint32_t clampedAt = 0;
    uint32_t cycles = 0;
    Spy::taskDelayHook = [&] {
        const uint32_t now = millis();
        ++cycles;
        assert(Spy::lastDelayTicks == 20);
        if (now < 1000) assert(Spy::duty.at(0) == 0); // Startup remains inhibited.
        if (now == 1000) {
            assert(Spy::duty.at(0) > 0);
            assert(Spy::duty.at(2) == 2000ULL * 65535 / 20000);
        }
        if (!faultObservedAt && device.pit.registers[15] == 1) faultObservedAt = now;
        if (faultObservedAt && now % 1000 == 0) {
            // The first service sample containing the hardware fault must clamp.
            assert(Spy::duty.at(0) == 0);
            if (!clampedAt) {
                clampedAt = now;
                assert(Spy::duty.at(2) == 1000ULL * 65535 / 20000);
            }
        }
    };
    Spy::delaysBeforeYield = 200;
    try { Spy::tasks[0].entry(Spy::tasks[0].context); } catch (const Spy::TaskYield&) {}
    Spy::taskDelayHook = {};
    assert(cycles == 201);
    // Conversion completion can precede its next 20 ms polling tick, so a
    // disconnect in that gap is found by the following completed conversion.
    assert(faultObservedAt >= disconnectAt && faultObservedAt - disconnectAt <= 200);
    assert(clampedAt >= faultObservedAt && clampedAt - faultObservedAt < 1000);
    assert(device.pit.completedAt.size() >= 20);
    for (size_t n = 1; n < device.pit.completedAt.size(); ++n)
        assert(device.pit.completedAt[n] - device.pit.completedAt[n - 1] == 180);
    assert(Spy::count("detach", 26) == 1); // Servo housekeeping continues during conversions.
    std::cout << "MAX31856 phase " << disconnectAt << ": fault at " << faultObservedAt
              << " ms, control clamp at " << clampedAt << " ms\n";
}
