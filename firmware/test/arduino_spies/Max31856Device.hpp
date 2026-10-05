#pragma once
#include "SPI.h"
#include <array>
#include <optional>

// Stateful external peripheral boundary, using the MAX31856 register protocol.
// A one-shot takes the datasheet maximum 155 ms + 15 ms open-circuit test.
// Temperature/fault registers remain unchanged until that conversion completes.
class Max31856Device {
public:
    struct Chip {
        std::array<uint8_t, 16> registers{};
        uint32_t rawTemperature{0x064000}; // 100 C
        uint8_t fault{0};
        bool stuck{false};
        uint32_t conversionStarted{0};
        std::optional<uint32_t> disconnectedAt;
        std::vector<uint32_t> completedAt;
    };
    Chip pit;
    Chip food;
    Max31856Device() {
        Spy::transactionHook = [this] { addressPending_ = true; };
        Spy::transferHook = [this](int cs, uint8_t value) { return transfer(cs, value); };
    }
    ~Max31856Device() { Spy::transactionHook = {}; Spy::transferHook = {}; }
private:
    uint8_t transfer(int cs, uint8_t value) {
        assert(cs == 5 || cs == 21);
        Chip& chip = cs == 5 ? pit : food;
        if ((chip.registers[0] & 0x40) && !chip.stuck &&
                millis() - chip.conversionStarted >= 170) {
            const uint32_t completed = chip.conversionStarted + 170;
            chip.completedAt.push_back(completed);
            chip.registers[0] &= ~0x40;
            chip.registers[12] = static_cast<uint8_t>(chip.rawTemperature >> 16);
            chip.registers[13] = static_cast<uint8_t>(chip.rawTemperature >> 8);
            chip.registers[14] = static_cast<uint8_t>(chip.rawTemperature);
            chip.registers[15] = chip.fault;
            if ((chip.registers[0] & 0x30) && chip.disconnectedAt && *chip.disconnectedAt <= completed)
                chip.registers[15] |= 1;
        }
        if (addressPending_) {
            write_ = value & 0x80;
            address_ = value & 0x7F;
            addressPending_ = false;
            return 0;
        }
        assert(address_ < chip.registers.size());
        const uint8_t address = address_++;
        if (!write_) return chip.registers[address];
        chip.registers[address] = value;
        if (address == 0 && (value & 0x40)) chip.conversionStarted = millis();
        return 0;
    }
    bool addressPending_{true};
    bool write_{false};
    uint8_t address_{0};
};
