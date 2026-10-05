#pragma once
#include "../../Services/Ports/TemperatureSensorPort.hpp"
#include "../Hardware/SharedSpiBus.hpp"

namespace SmokerController::Adapters::Sensors {
// MAX31856 rev. 0, pp. 15, 18-20, 24-25:
// https://www.analog.com/media/en/technical-documentation/data-sheets/max31856.pdf
// SPI mode 1 (CPHA=1), MSB first, 1 MHz (below 5 MHz limit).
// CR1=0x03: K type, one sample; CR0=0x90: continuous, open detection,
// cold junction enabled, comparator faults, 60 Hz filter. MOSI is required.
class MAX31856SensorAdapter final : public Services::Ports::ITemperatureSensorPort {
public:
    explicit MAX31856SensorAdapter(Hardware::SharedSpiBus& bus,
                                   uint8_t cs_pit = 5, int8_t cs_food1 = -1) noexcept
        : bus_(bus), cs_pit_(cs_pit), cs_food1_(cs_food1) {}
    void begin() noexcept {
#ifdef ARDUINO
        // No other chip may be selected during the first configuration frame.
        pinMode(cs_pit_, OUTPUT);
        digitalWrite(cs_pit_, HIGH);
        if (cs_food1_ >= 0) { pinMode(cs_food1_, OUTPUT); digitalWrite(cs_food1_, HIGH); }
        pit_ready_ = initializeChip(cs_pit_);
        if (cs_food1_ >= 0) food_ready_ = initializeChip(static_cast<uint8_t>(cs_food1_));
        started_ms_ = millis();
#endif
    }
    Domain::TemperatureReading readTemperature(Domain::SensorRole role) noexcept override {
        uint32_t now = 0;
#ifdef ARDUINO
        now = millis();
#endif
        if (role != Domain::SensorRole::Pit && (role != Domain::SensorRole::Food1 || cs_food1_ < 0))
            return Domain::TemperatureReading{0.0f, role, now, Domain::SensorFault::Disconnected};
#ifdef ARDUINO
        if (!(role == Domain::SensorRole::Pit ? pit_ready_ : food_ready_))
            return Domain::TemperatureReading{0.0f, role, now, Domain::SensorFault::Disconnected};
        // Keep regulation inhibited until the initial conversion/open test completes.
        if (now - started_ms_ < 250) return Domain::TemperatureReading{0.0f, role, now, Domain::SensorFault::Stale};
        const uint8_t cs = role == Domain::SensorRole::Pit ? cs_pit_ : static_cast<uint8_t>(cs_food1_);
        Hardware::SharedSpiBus::Frame frame(bus_, cs, 1000000, SPI_MODE1);
        if (!frame) return Domain::TemperatureReading{0.0f, role, now, Domain::SensorFault::Disconnected};
        frame.transfer(0x0C); // contiguous LTCBH, LTCBM, LTCBL, fault status
        uint32_t raw = frame.transfer(0);
        raw = (raw << 8) | frame.transfer(0);
        raw = (raw << 8) | frame.transfer(0);
        const uint8_t fault = frame.transfer(0);
        return decodeRawReading(raw, fault, role, now);
#else
        return Domain::TemperatureReading{0.0f, role, now, Domain::SensorFault::Disconnected};
#endif
    }
    static Domain::TemperatureReading decodeRawReading(uint32_t raw, uint8_t status,
            Domain::SensorRole role, uint32_t now) noexcept {
        if (status) return Domain::TemperatureReading{0.0f, role, now, (status & 0x01) ? Domain::SensorFault::Disconnected : Domain::SensorFault::OutOfRange};
        int32_t signed_value = static_cast<int32_t>((raw >> 5) & 0x7FFFF);
        if (signed_value & 0x40000) signed_value -= 0x80000;
        return Domain::TemperatureReading{static_cast<float>(signed_value) / 128.0f, role, now, Domain::SensorFault::Ok};
    }
private:
#ifdef ARDUINO
    bool writeRegister(uint8_t cs, uint8_t address, uint8_t value) noexcept {
        Hardware::SharedSpiBus::Frame frame(bus_, cs, 1000000, SPI_MODE1);
        if (!frame) return false;
        frame.transfer(address | 0x80);
        frame.transfer(value);
        return true;
    }
    bool initializeChip(uint8_t cs) noexcept {
        pinMode(cs, OUTPUT);
        digitalWrite(cs, HIGH);
        if (!writeRegister(cs, 0x00, 0x00) || // Stop before changing averaging/filter.
            !writeRegister(cs, 0x01, 0x03) ||
            !writeRegister(cs, 0x02, 0x00) || // Unmask faults.
            !writeRegister(cs, 0x00, 0x90)) return false;
        Hardware::SharedSpiBus::Frame frame(bus_, cs, 1000000, SPI_MODE1);
        if (!frame) return false;
        frame.transfer(0x00);
        const uint8_t cr0 = frame.transfer(0);
        const uint8_t cr1 = frame.transfer(0);
        return cr0 == 0x90 && cr1 == 0x03;
    }
#endif
    Hardware::SharedSpiBus& bus_;
    uint8_t cs_pit_;
    int8_t cs_food1_;
    uint32_t started_ms_{0};
    bool pit_ready_{false};
    bool food_ready_{false};
};
}
