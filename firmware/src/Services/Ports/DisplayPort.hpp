#pragma once

#include "../../Domain/DisplayView.hpp"

namespace SmokerController::Services::Ports {

/**
 * Port contract for visual screen hardware (Inland E-Ink, OLED, LCD).
 * Follows Onion Architecture boundary rules: interface defined in Services.Ports,
 * concrete hardware drivers implemented under Adapters/Display.
 */
class IDisplayPort {
public:
    virtual ~IDisplayPort() = default;

    /**
     * Initialize display SPI bus and controller IC.
     */
    virtual void begin() = 0;

    /**
     * Render the given domain display view onto the screen.
     * @param view Domain model with temperatures, setpoints, outputs, and status.
     * @param force_full_refresh If true, forces a full waveform refresh to clear ghosting.
     */
    virtual void render(const Domain::DisplayView& view, bool force_full_refresh = false) = 0;

    /**
     * Clear the display to blank/white.
     */
    virtual void clear() = 0;

    /**
     * Put the display controller into low-power sleep mode (<5uA).
     */
    virtual void sleep() = 0;

    /**
     * Check if display is currently busy executing a refresh waveform.
     */
    [[nodiscard]] virtual bool isBusy() const = 0;
};

} // namespace SmokerController::Services::Ports
