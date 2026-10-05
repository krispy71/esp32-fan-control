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
    for (size_t i=0;i<Spy::events.size();++i) {
        if (Spy::events[i].operation == "nvsBegin") nvs=i;
        if (Spy::events[i].operation == "pwm" && Spy::events[i].a == 2) pwm=i;
        if (Spy::events[i].operation == "task" && Spy::events[i].a == 1) task=i;
    }
    assert(nvs < pwm && pwm < task);
    try { Spy::tasks[0].entry(Spy::tasks[0].context); } catch (const Spy::TaskYield&) {}
    assert(Spy::duty.at(0) == 0); // First conversion is not ready at task launch.
    Spy::now = 200;
    // Real control entry point: a healthy sample drives outputs, then a pit fault clamps.
    Spy::response = {0x04,0xB0,0,0,0x04,0xB0,0,0}; // 75 C => active demand below 225 F target
    try { Spy::tasks[0].entry(Spy::tasks[0].context); } catch (const Spy::TaskYield&) {}
    assert(Spy::duty.at(0) > 0);
    assert(Spy::lastDelayTicks == 20);
    Spy::clear(); Spy::now = 220; Spy::delaysBeforeYield = 79;
    Spy::response = {0x04,0xB0,0,0,0x04,0xB0,0,0};
    try { Spy::tasks[0].entry(Spy::tasks[0].context); } catch (const Spy::TaskYield&) {}
    assert(Spy::count("transfer", 5) == 8); // 1Hz acquisition across 80 x 20ms ticks.
    assert(Spy::count("detach", 26) == 1); // 1.5s idle deadline serviced on owning task.

    Spy::now = 2000; Spy::response = {0,1,0,1};
    try { Spy::tasks[0].entry(Spy::tasks[0].context); } catch (const Spy::TaskYield&) {}
    assert(Spy::duty.at(0) == 0 && Spy::duty.at(2) == 2200ULL * 65535 / 20000);
    try { Spy::tasks[1].entry(Spy::tasks[1].context); } catch (const Spy::TaskYield&) {}
    assert(Spy::count("networkBegin") == 1);
    Spy::clear(); loop(); assert(Spy::events.empty());
    std::cout << "Production setup and task composition checks passed\n";
}
