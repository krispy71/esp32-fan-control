#pragma once
#include "Arduino.h"
#include <deque>
#include <thread>
#define VSPI 3
#define MSBFIRST 1
#define SPI_MODE0 0
#define SPI_MODE1 1
struct SPISettings { uint32_t hz; int order; int mode; SPISettings(uint32_t h, int o, int m) : hz(h), order(o), mode(m) {} };
namespace Spy { inline std::deque<uint8_t> response; inline bool yieldTransfer = false; }
class SPIClass {
public:
    explicit SPIClass(int) {}
    void begin(int sck, int miso, int mosi, int) { assert(sck == 18 && miso == 19 && mosi == 23); Spy::event("spiBegin", sck, mosi); }
    void beginTransaction(SPISettings settings) { assert(Spy::busLocked && Spy::activeCs == -1); assert(settings.order == MSBFIRST); Spy::transaction = true; Spy::event("transaction", settings.mode, settings.hz); }
    void endTransaction() { assert(Spy::busLocked && Spy::activeCs == -1); Spy::event("endTransaction"); Spy::transaction = false; }
    uint8_t transfer(uint8_t value) {
        assert(Spy::transaction && Spy::busLocked && Spy::activeCs >= 0);
        Spy::event("transfer", Spy::activeCs, value);
        if (Spy::yieldTransfer) std::this_thread::yield();
        if (Spy::response.empty()) return 0;
        uint8_t result = Spy::response.front(); Spy::response.pop_front(); return result;
    }
};
