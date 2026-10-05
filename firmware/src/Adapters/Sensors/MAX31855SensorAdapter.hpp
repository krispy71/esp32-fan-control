#pragma once

#include "../../Services/Ports/TemperatureSensorPort.hpp"
#include <cstdint>
#include "../Hardware/SharedSpiBus.hpp"

#ifdef ARDUINO
#include <Arduino.h>
#include <SPI.h>
#endif

namespace SmokerController::Adapters::Sensors {

class MAX31855SensorAdapter : public Services::Ports::ITemperatureSensorPort {
public:
    explicit MAX31855SensorAdapter(Hardware::SharedSpiBus& bus,
                                   uint8_t cs_pit_pin = 5, int8_t cs_food1_pin = -1) noexcept
        : bus_(bus), cs_pit_(cs_pit_pin), cs_food1_(cs_food1_pin) {}

    void begin() noexcept {
#ifdef ARDUINO
        pinMode(cs_pit_, OUTPUT);
        digitalWrite(cs_pit_, HIGH);
        if (cs_food1_ >= 0) {
            pinMode(cs_food1_, OUTPUT);
            digitalWrite(cs_food1_, HIGH);
        }
        started_ms_ = millis();
#endif
        begun_ = true;
    }

    Domain::TemperatureReading readTemperature(Domain::SensorRole role) noexcept override {
        uint32_t now_ms = 0;
#ifdef ARDUINO
        now_ms = millis();
#endif
        if (role != Domain::SensorRole::Pit && (role != Domain::SensorRole::Food1 || cs_food1_ < 0))
            return Domain::TemperatureReading{0.0f, role, now_ms, Domain::SensorFault::Disconnected};
        // MAX31855 datasheet rev. 5 p.4 specifies tCONV_PU = 200 ms.
        // https://www.analog.com/media/en/technical-documentation/data-sheets/MAX31855.pdf
        if (!begun_ || now_ms - started_ms_ < 200)
            return Domain::TemperatureReading{0.0f, role, now_ms, Domain::SensorFault::Stale};
        const uint8_t cs = role == Domain::SensorRole::Pit ? cs_pit_ : static_cast<uint8_t>(cs_food1_);
        return decodeRawReading(readRaw32(cs), role, now_ms);
    }

    static Domain::TemperatureReading decodeRawReading(
        uint32_t raw,
        Domain::SensorRole role,
        uint32_t timestamp_ms
    ) noexcept {
        // Bit 16 is the master fault bit
        if (raw & 0x00010000ULL) {
            // Probe fault detected
            Domain::SensorFault fault = Domain::SensorFault::OutOfRange;
            if (raw & 0x01) {
                fault = Domain::SensorFault::Disconnected; // Open circuit
            } else if (raw & 0x02) {
                fault = Domain::SensorFault::ShortToGnd;    // Short to GND
            } else if (raw & 0x04) {
                fault = Domain::SensorFault::ShortToVcc;    // Short to VCC
            }
            return Domain::TemperatureReading{0.0f, role, timestamp_ms, fault};
        }

        // Bits 31..18: 14-bit signed thermocouple temperature (0.25°C resolution)
        int32_t tc_raw = (raw >> 18) & 0x3FFF;
        // Sign extend if negative (bit 13 set)
        if (tc_raw & 0x2000) {
            tc_raw |= 0xFFFFC000;
        }

        float celsius = static_cast<float>(tc_raw) * 0.25f;
        return Domain::TemperatureReading{celsius, role, timestamp_ms, Domain::SensorFault::Ok};
    }

private:
    uint32_t readRaw32(uint8_t cs_pin) noexcept {
#ifdef ARDUINO
        Hardware::SharedSpiBus::Frame frame(bus_, cs_pin, 4000000, SPI_MODE0);
        if (!frame) return 0x00010001; // unavailable bus fails closed
        delayMicroseconds(1);
        uint32_t raw = 0;
        for (uint8_t i = 0; i < 4; ++i) raw = (raw << 8) | frame.transfer(0);
        return raw;
#else
        (void)cs_pin;
        return 0;
#endif
    }

    Hardware::SharedSpiBus& bus_;
    uint8_t cs_pit_;
    int8_t cs_food1_;
    uint32_t started_ms_{0};
    bool begun_{false};
};

} // namespace SmokerController::Adapters::Sensors
