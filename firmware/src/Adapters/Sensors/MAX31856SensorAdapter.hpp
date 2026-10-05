#pragma once
#include "../../Services/Ports/TemperatureSensorPort.hpp"
#include "../Hardware/SharedSpiBus.hpp"

namespace SmokerController::Adapters::Sensors {
// MAX31856 rev. 0, pp. 15, 18-20, 24-25:
// https://www.analog.com/media/en/technical-documentation/data-sheets/max31856.pdf
// SPI mode 1 (CPHA=1), MSB first, 1 MHz (below 5 MHz limit).
// CR1=0x03: K type, one sample; CR0=0x10: normally off, open detection,
// cold junction enabled, comparator faults, 60 Hz filter. MOSI is required.
// Each one-shot includes an open-circuit test. Continuous mode only tests every
// 16 conversions, which can leave the pit disconnected through a control sample.
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
        initializeConversion(pit_, cs_pit_);
        if (cs_food1_ >= 0) initializeConversion(food_, static_cast<uint8_t>(cs_food1_));
#endif
    }
    // Called on the control owner task at 20 ms cadence; conversion waits never
    // block servo housekeeping. A completed, fault-checked reading is cached.
    void update() noexcept {
#ifdef ARDUINO
        updateConversion(pit_, cs_pit_);
        if (cs_food1_ >= 0) updateConversion(food_, static_cast<uint8_t>(cs_food1_));
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
        const auto& conversion = role == Domain::SensorRole::Pit ? pit_ : food_;
        if (!conversion.configured)
            return Domain::TemperatureReading{0.0f, role, now, Domain::SensorFault::Disconnected};
        auto reading = conversion.reading;
        reading.role = role;
        // A missed upkeep call or hung conversion must not preserve old health.
        if (now - reading.timestamp_ms > FRESHNESS_MS) reading.fault = Domain::SensorFault::Stale;
        return reading;
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
    struct Conversion {
        bool configured{false};
        bool pending{false};
        uint32_t started_ms{0};
        Domain::TemperatureReading reading{0.0f, Domain::SensorRole::Pit, 0, Domain::SensorFault::Stale};
    };
    // Datasheet pp. 4,14: first/one-shot conversion <=155 ms at 60 Hz,
    // open-circuit test <=15 ms with cold-junction sensing enabled.
    static constexpr uint32_t CONVERSION_WAIT_MS = 170;
    static constexpr uint32_t CONVERSION_TIMEOUT_MS = 250;
    static constexpr uint32_t FRESHNESS_MS = 500;
#ifdef ARDUINO
    void initializeConversion(Conversion& conversion, uint8_t cs) noexcept {
        conversion = Conversion{};
        conversion.configured = initializeChip(cs);
        if (conversion.configured) startConversion(conversion, cs);
    }
    void failConversion(Conversion& conversion, Domain::SensorFault fault) noexcept {
        conversion.reading = Domain::TemperatureReading{0.0f, Domain::SensorRole::Pit,
                                                       static_cast<uint32_t>(millis()), fault};
        conversion.pending = false;
    }
    void startConversion(Conversion& conversion, uint8_t cs) noexcept {
        if (!writeRegister(cs, 0x00, 0x50)) {
            failConversion(conversion, Domain::SensorFault::Disconnected);
            return;
        }
        conversion.started_ms = millis();
        conversion.pending = true;
    }
    void updateConversion(Conversion& conversion, uint8_t cs) noexcept {
        if (!conversion.configured) return;
        if (!conversion.pending) { startConversion(conversion, cs); return; }
        const uint32_t elapsed = millis() - conversion.started_ms;
        if (elapsed < CONVERSION_WAIT_MS) return;
        uint8_t cr0 = 0;
        {
            Hardware::SharedSpiBus::Frame frame(bus_, cs, 1000000, SPI_MODE1);
            if (!frame) { failConversion(conversion, Domain::SensorFault::Disconnected); return; }
            frame.transfer(0x00);
            cr0 = frame.transfer(0);
        }
        if (cr0 == 0x50) { // One-shot bit clears only when fresh data is ready.
            if (elapsed >= CONVERSION_TIMEOUT_MS) failConversion(conversion, Domain::SensorFault::Stale);
            return;
        }
        if (cr0 != 0x10) { // Reset, missing chip, or lost conversion configuration.
            failConversion(conversion, Domain::SensorFault::Disconnected);
            conversion.configured = false;
            return;
        }
        {
            Hardware::SharedSpiBus::Frame frame(bus_, cs, 1000000, SPI_MODE1);
            if (!frame) { failConversion(conversion, Domain::SensorFault::Disconnected); return; }
            frame.transfer(0x0C); // Atomic LTCBH, LTCBM, LTCBL, fault status read.
            uint32_t raw = frame.transfer(0);
            raw = (raw << 8) | frame.transfer(0);
            raw = (raw << 8) | frame.transfer(0);
            const uint8_t fault = frame.transfer(0);
            conversion.reading = decodeRawReading(raw, fault, Domain::SensorRole::Pit, millis());
        }
        startConversion(conversion, cs);
    }
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
            !writeRegister(cs, 0x00, 0x10)) return false;
        Hardware::SharedSpiBus::Frame frame(bus_, cs, 1000000, SPI_MODE1);
        if (!frame) return false;
        frame.transfer(0x00);
        const uint8_t cr0 = frame.transfer(0);
        const uint8_t cr1 = frame.transfer(0);
        return cr0 == 0x10 && cr1 == 0x03;
    }
#endif
    Hardware::SharedSpiBus& bus_;
    uint8_t cs_pit_;
    int8_t cs_food1_;
    Conversion pit_;
    Conversion food_;
};
}
