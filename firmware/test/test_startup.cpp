#include STARTUP_ENTRYPOINT
#include <cassert>
#include <iostream>
int main() {
    Spy::startupAllowed = false;
    { Application construction; } // No adapter touches Arduino, SPI or NVS in constructors.
    Spy::startupAllowed = true;
    Domain::SmokerConfig cfg; cfg.servo_min_pulse_us=800; cfg.servo_max_pulse_us=2200; cfg.servo_inverted=true;
    Adapters::Storage::ESP32NVSConfigAdapter storage; assert(storage.saveConfig(cfg));
    Spy::clear(); setup();
    assert(Spy::tasks.size() == 2);
    assert(Spy::tasks[0].core == 1 && Spy::tasks[1].core == 0);
    assert(Spy::tasks[0].context == Spy::tasks[1].context && Spy::tasks[0].context != nullptr);
    assert(Spy::duty.at(0) == 0 && Spy::duty.at(2) == 2200ULL * 65535 / 20000);
    size_t nvs=0, pwm=0, task=0;
    bool first_servo_pulse = true;
    for (size_t i=0;i<Spy::events.size();++i) {
        if (Spy::events[i].operation == "nvsBegin") nvs=i;
        if (Spy::events[i].operation == "pwm" && Spy::events[i].a == 2) {
            if (first_servo_pulse) {
                assert(Spy::events[i].b == 2200ULL * 65535 / 20000);
                pwm = i;
                first_servo_pulse = false;
            }
        }
        if (Spy::events[i].operation == "task" && Spy::events[i].a == 1) task=i;
    }
    assert(nvs < pwm && pwm < task);
    assert(Spy::count("pwm", 2) == 1); // No default pulse may precede calibrated closure.
    try { Spy::tasks[0].entry(Spy::tasks[0].context); } catch (const Spy::TaskYield&) {}
    assert(Spy::duty.at(0) == 0); // First conversion is not ready at task launch.
    Spy::now = 200;
    Spy::transferDelayMs = 1; // Crossing a clock tick during actual acquisition must not detach.
    // Real control entry point: a healthy sample drives outputs, then a pit fault clamps.
    Spy::response = {0x04,0xB0,0,0,0x04,0xB0,0,0}; // 75 C => active demand below 225 F target
    try { Spy::tasks[0].entry(Spy::tasks[0].context); } catch (const Spy::TaskYield&) {}
    assert(Spy::duty.at(0) > 0);
    assert(Spy::duty.at(2) == 800ULL * 65535 / 20000);
    assert(Spy::count("detach", 26) == 0);
    Spy::transferDelayMs = 0;
    assert(Spy::lastDelayTicks == 20);
    Spy::clear(); Spy::now = 220; Spy::delaysBeforeYield = 79;
    uint32_t frame = 0x04B00000; unsigned byte = 0;
    Spy::transferHook = [&](int cs, uint8_t) {
        assert(cs == 5);
        return static_cast<uint8_t>(frame >> (24 - 8 * (byte++ % 4)));
    };
    try { Spy::tasks[0].entry(Spy::tasks[0].context); } catch (const Spy::TaskYield&) {}
    assert(Spy::count("transfer", 5) == 328); // 2 PID samples + 80 fast pit-health checks.
    assert(Spy::count("detach", 26) == 1); // 1.5s idle deadline serviced on owning task.

    // Disconnect between PID samples. Healthy data must not hold the fan on
    // until the next full cycle at 3000 ms.
    Spy::now = 2000; Spy::delaysBeforeYield = 5;
    Spy::taskDelayHook = [&] {
        if (millis() == 2000) assert(Spy::duty.at(0) > 0);
        if (millis() == 2020) frame = 0x00010001;
        if (millis() >= 2040) assert(Spy::duty.at(0) == 0);
    };
    try { Spy::tasks[0].entry(Spy::tasks[0].context); } catch (const Spy::TaskYield&) {}
    assert(Spy::duty.at(0) == 0 && Spy::duty.at(2) == 2200ULL * 65535 / 20000);
    Spy::transferHook = {}; Spy::taskDelayHook = {};
    try { Spy::tasks[1].entry(Spy::tasks[1].context); } catch (const Spy::TaskYield&) {}
    assert(Spy::count("networkBegin") == 1);
    Spy::clear(); loop(); assert(Spy::events.empty());
    std::cout << "Production setup and task composition checks passed\n";
}
