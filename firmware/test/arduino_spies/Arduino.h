#pragma once
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#define OUTPUT 1
#define INPUT 0
#define LOW 0
#define HIGH 1
namespace Spy {
struct Event { std::string operation; int a; uint32_t b; };
inline std::mutex eventsMutex;
inline std::vector<Event> events;
inline std::atomic<uint32_t> now{0};
inline std::atomic<int> activeCs{-1};
inline thread_local bool busLocked = false;
inline thread_local bool transaction = false;
inline bool startupAllowed = true;
inline void event(const char* op, int a = 0, uint32_t b = 0) {
    assert(startupAllowed);
    std::lock_guard<std::mutex> lock(eventsMutex);
    events.push_back({op, a, b});
}
inline void clear() { std::lock_guard<std::mutex> lock(eventsMutex); events.clear(); }
inline size_t count(const char* operation, int a = -999) {
    std::lock_guard<std::mutex> lock(eventsMutex);
    return static_cast<size_t>(std::count_if(events.begin(), events.end(), [&](const Event& e) {
        return e.operation == operation && (a == -999 || a == e.a);
    }));
}
}
inline uint32_t millis() { return Spy::now; }
inline void delay(uint32_t time) { Spy::now += time; }
inline void delayMicroseconds(uint32_t) {}
inline void pinMode(int pin, int mode) { Spy::event("pinMode", pin, mode); }
inline void digitalWrite(int pin, int value) {
    if (pin == 4 || pin == 5 || pin == 21) {
        if (value == LOW) {
            assert(Spy::busLocked && Spy::transaction);
            int expected = -1;
            assert(Spy::activeCs.compare_exchange_strong(expected, pin));
        } else if (Spy::activeCs == pin) {
            assert(Spy::busLocked && Spy::transaction);
            Spy::activeCs = -1;
        }
    }
    Spy::event("gpio", pin, value);
}
inline int digitalRead(int) { return LOW; }
struct SerialSpy {
    void begin(int) { Spy::event("serial"); }
    template<class... T> void printf(const char*, T...) {}
    void println(const char*) {}
};
inline SerialSpy Serial;
#include "freertos/FreeRTOS.h"
namespace Spy {
struct Task { void (*entry)(void*); void* context; int core; };
inline std::vector<Task> tasks;
struct TaskYield {};
inline unsigned delaysBeforeYield = 0;
inline uint32_t lastDelayTicks = 0;
}
inline uint32_t xTaskGetTickCount() { return millis(); }
inline int xTaskCreatePinnedToCore(void (*entry)(void*), const char*, uint32_t, void* context, int, void*, int core) {
    Spy::event("task", core); Spy::tasks.push_back({entry, context, core}); return pdPASS;
}
inline void vTaskDelayUntil(TickType_t*, TickType_t ticks) {
    Spy::lastDelayTicks = ticks; Spy::now += ticks;
    if (Spy::delaysBeforeYield > 0) { --Spy::delaysBeforeYield; return; }
    throw Spy::TaskYield{};
}
inline void vTaskDelay(TickType_t) { throw Spy::TaskYield{}; }
