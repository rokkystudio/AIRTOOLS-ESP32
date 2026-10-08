#pragma once

#include <Arduino.h>
#include "AirtoolsConfig.h"

#if AIRTOOLS_HAS_DISPLAY && AIRTOOLS_HAS_JD9853
#include <Arduino_GFX_Library.h>
#endif

/**
 * Provides a minimal local status screen on boards that declare a supported display.
 */
class DisplayStatus
{
public:
    /**
     * Initializes the optional display and renders the initial AIRTOOLS status.
     */
    void begin();

    /**
     * Updates the large runtime status shown on the optional display.
     */
    void loop(
        bool bleConnected,
        bool bleAdvertising,
        bool wifiScanning,
        int wifiChannel,
        const char *targetLabel,
        bool replayActive,
        int batteryMillivolts
    );

private:
#if AIRTOOLS_HAS_DISPLAY && AIRTOOLS_HAS_JD9853
    void renderConnection(bool bleConnected, bool bleAdvertising);
    void renderTarget(bool wifiScanning, const char *targetLabel);
    void renderChannel(bool wifiScanning, int wifiChannel);
    void renderReplay(bool replayActive);
    void renderBattery(int batteryMillivolts);

    Arduino_DataBus *bus = nullptr;
    Arduino_GFX *gfx = nullptr;
    Arduino_Canvas *channelCanvas = nullptr;
    Arduino_Canvas *batteryCanvas = nullptr;
    bool ready = false;
    bool lastBleConnected = false;
    bool lastBleAdvertising = false;
    bool lastWifiScanning = false;
    int lastWifiChannel = 0;
    char lastTargetLabel[25] = {};
    bool lastReplayActive = false;
    int lastBatteryMillivolts = -1;
    uint32_t lastBatteryUpdateMs = 0;
    bool stateInitialized = false;
#endif
};
