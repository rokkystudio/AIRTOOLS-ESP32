#pragma once

#include <Arduino.h>

/**
 * Wraps board-specific peripherals so the firmware runs on display and non-display boards.
 */
class BoardSupport
{
public:
    /**
     * Starts serial diagnostics and optional board peripherals declared by build flags.
     */
    void begin();

    /**
     * Updates optional display/status peripherals from the main loop.
     */
    void loop();

    /**
     * Returns board and battery details for /status.
     */
    String formatStatus() const;

    /**
     * Returns the current battery voltage in millivolts, or -1 when unavailable.
     */
    int readBatteryMillivolts() const;
};
