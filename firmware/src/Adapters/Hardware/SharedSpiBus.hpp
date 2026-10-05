#pragma once
#include <cstdint>
#ifdef ARDUINO
#include <Arduino.h>
#include <SPI.h>
#include <freertos/semphr.h>
#endif

namespace SmokerController::Adapters::Hardware {
// One explicitly owned bus is injected into every peripheral. A frame owns the
// bus before CS goes low, and releases it only after CS returns high.
class SharedSpiBus {
public:
    SharedSpiBus() = default;
    ~SharedSpiBus() {
#ifdef ARDUINO
        if (mutex_) vSemaphoreDelete(mutex_);
#endif
    }
    SharedSpiBus(const SharedSpiBus&) = delete;
    SharedSpiBus& operator=(const SharedSpiBus&) = delete;
    bool begin(int8_t sck = 18, int8_t miso = 19, int8_t mosi = 23) noexcept {
#ifdef ARDUINO
        if (mutex_) return true;
        mutex_ = xSemaphoreCreateMutex();
        if (!mutex_) return false;
        spi_.begin(sck, miso, mosi, -1);
#else
        (void)sck; (void)miso; (void)mosi;
#endif
        return true;
    }
#ifdef ARDUINO
    class Frame {
    public:
        Frame(SharedSpiBus& bus, uint8_t cs, uint32_t hz, uint8_t mode) noexcept
            : bus_(bus), cs_(cs) {
            if (bus_.mutex_ && xSemaphoreTake(bus_.mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
                acquired_ = true;
                bus_.spi_.beginTransaction(SPISettings(hz, MSBFIRST, mode));
                digitalWrite(cs_, LOW);
            }
        }
        ~Frame() {
            if (!acquired_) return;
            digitalWrite(cs_, HIGH);
            bus_.spi_.endTransaction();
            xSemaphoreGive(bus_.mutex_);
        }
        Frame(const Frame&) = delete;
        Frame& operator=(const Frame&) = delete;
        explicit operator bool() const noexcept { return acquired_; }
        uint8_t transfer(uint8_t value) noexcept { return acquired_ ? bus_.spi_.transfer(value) : 0xFF; }
    private:
        SharedSpiBus& bus_;
        uint8_t cs_;
        bool acquired_{false};
    };
#endif
private:
#ifdef ARDUINO
    SPIClass spi_{VSPI};
    SemaphoreHandle_t mutex_{nullptr};
#endif
};
}
