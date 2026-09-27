#pragma once

#include "../../Services/Ports/TemperatureSensorPort.hpp"
#include <cstdint>

#ifdef ARDUINO
#include <Arduino.h>
#include <SPI.h>
#endif

namespace SmokerController::Adapters::Sensors {

class MAX31855SensorAdapter : public Services::Ports::ITemperatureSensorPort {
public:
    MAX31855SensorAdapter(
        uint8_t cs_pit_pin = 5,
        uint8_t cs_food1_pin = 21,
        uint8_t sck_pin = 18,
        uint8_t miso_pin = 19
    ) noexcept
        : cs_pit_(cs_pit_pin),
          cs_food1_(cs_food1_pin),
          sck_(sck_pin),
          miso_(miso_pin) {}

    void begin() noexcept {
#ifdef ARDUINO
        pinMode(cs_pit_, OUTPUT);
        digitalWrite(cs_pit_, HIGH);

        pinMode(cs_food1_, OUTPUT);
        digitalWrite(cs_food1_, HIGH);

        SPI.begin(sck_, miso_, -1, cs_pit_);
#endif
    }

    Domain::TemperatureReading readTemperature(Domain::SensorRole role) noexcept override {
        uint8_t cs_pin = (role == Domain::SensorRole::Pit) ? cs_pit_ : cs_food1_;
        uint32_t now_ms = 0;
#ifdef ARDUINO
        now_ms = millis();
#endif

        uint32_t raw_data = readRaw32(cs_pin);
        return decodeRawReading(raw_data, role, now_ms);
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
        digitalWrite(cs_pin, LOW);
        delayMicroseconds(1);

        uint32_t b0 = SPI.transfer(0x00);
        uint32_t b1 = SPI.transfer(0x00);
        uint32_t b2 = SPI.transfer(0x00);
        uint32_t b3 = SPI.transfer(0x00);

        digitalWrite(cs_pin, HIGH);
        return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
#else
        (void)cs_pin;
        return 0;
#endif
    }

    uint8_t cs_pit_;
    uint8_t cs_food1_;
    uint8_t sck_;
    uint8_t miso_;
};

} // namespace SmokerController::Adapters::Sensors
