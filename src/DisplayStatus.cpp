#include "DisplayStatus.h"

#if AIRTOOLS_HAS_DISPLAY && AIRTOOLS_HAS_JD9853

namespace
{
constexpr uint16_t COLOR_BLACK = 0x0000;
constexpr uint16_t COLOR_WHITE = 0xFFFF;
constexpr uint16_t COLOR_GREEN = 0x07E0;
constexpr uint16_t COLOR_YELLOW = 0xFFE0;

constexpr int STATUS_X = 10;
constexpr int BLE_Y = 34;
constexpr int TARGET_Y = 64;
constexpr int CHANNEL_Y = 94;
constexpr int BATTERY_Y = 124;
constexpr int REPLAY_Y = 150;
constexpr int LARGE_ROW_HEIGHT = 20;
constexpr uint32_t BATTERY_UPDATE_INTERVAL_MS = 1000;
constexpr int BATTERY_UPDATE_THRESHOLD_MV = 50;
}

/**
 * Initializes the JD9853-compatible panel through the ST7789 command set.
 */
void DisplayStatus::begin()
{
    if (AIRTOOLS_LCD_CS_PIN < 0 ||
        AIRTOOLS_LCD_DC_PIN < 0 ||
        AIRTOOLS_LCD_RST_PIN < 0 ||
        AIRTOOLS_LCD_SCK_PIN < 0 ||
        AIRTOOLS_LCD_MOSI_PIN < 0 ||
        AIRTOOLS_LCD_BL_PIN < 0) {
        Serial.println("DISPLAY_CONFIG_INVALID");
        return;
    }

#if AIRTOOLS_HAS_SD
    if (AIRTOOLS_SD_CS_PIN >= 0) {
        pinMode(AIRTOOLS_SD_CS_PIN, OUTPUT);
        digitalWrite(AIRTOOLS_SD_CS_PIN, HIGH);
    }
#endif

    pinMode(AIRTOOLS_LCD_CS_PIN, OUTPUT);
    digitalWrite(AIRTOOLS_LCD_CS_PIN, HIGH);

    bus = new Arduino_HWSPI(
        AIRTOOLS_LCD_DC_PIN,
        AIRTOOLS_LCD_CS_PIN,
        AIRTOOLS_LCD_SCK_PIN,
        AIRTOOLS_LCD_MOSI_PIN
    );
    gfx = new Arduino_ST7789(
        bus,
        AIRTOOLS_LCD_RST_PIN,
        0,
        false,
        AIRTOOLS_LCD_WIDTH,
        AIRTOOLS_LCD_HEIGHT,
        34,
        0,
        34,
        0
    );

    if (!bus || !gfx || !gfx->begin()) {
        Serial.println("DISPLAY_INIT_FAILED");
        return;
    }

    gfx->setRotation(7);
    gfx->fillScreen(COLOR_BLACK);
    gfx->setTextWrap(false);
    gfx->setTextColor(COLOR_WHITE);
    gfx->setTextSize(2);
    gfx->setCursor(STATUS_X, 6);
    gfx->println("AIRTOOLS");

    channelCanvas = new Arduino_Canvas(
        gfx->width() - STATUS_X,
        LARGE_ROW_HEIGHT,
        gfx,
        STATUS_X,
        CHANNEL_Y
    );
    batteryCanvas = new Arduino_Canvas(
        gfx->width() - STATUS_X,
        LARGE_ROW_HEIGHT,
        gfx,
        STATUS_X,
        BATTERY_Y
    );
    if (!channelCanvas || !batteryCanvas ||
        !channelCanvas->begin(GFX_SKIP_OUTPUT_BEGIN) ||
        !batteryCanvas->begin(GFX_SKIP_OUTPUT_BEGIN)) {
        Serial.println("DISPLAY_CANVAS_FAILED");
        return;
    }

    if (!ledcAttach(AIRTOOLS_LCD_BL_PIN, 5000, 8)) {
        Serial.println("DISPLAY_BACKLIGHT_PWM_FAILED");
        return;
    }
    ledcWrite(AIRTOOLS_LCD_BL_PIN, 128);

    ready = true;
    loop(false, false, false, 1, "ALL", false, -1);
    Serial.println("DISPLAY_READY JD9853 172x320");
}

/**
 * Updates large status rows only when their visible content changes.
 */
void DisplayStatus::loop(
    bool bleConnected,
    bool bleAdvertising,
    bool wifiScanning,
    int wifiChannel,
    const char *targetLabel,
    bool replayActive,
    int batteryMillivolts
)
{
    if (!ready) {
        return;
    }

    const char *safeTarget = targetLabel && targetLabel[0] ? targetLabel : "ALL";

    if (!stateInitialized ||
        bleConnected != lastBleConnected ||
        bleAdvertising != lastBleAdvertising) {
        renderConnection(bleConnected, bleAdvertising);
    }

    if (!stateInitialized ||
        wifiScanning != lastWifiScanning ||
        strncmp(safeTarget, lastTargetLabel, sizeof(lastTargetLabel) - 1) != 0) {
        renderTarget(wifiScanning, safeTarget);
    }

    if (!stateInitialized ||
        wifiScanning != lastWifiScanning ||
        wifiChannel != lastWifiChannel) {
        renderChannel(wifiScanning, wifiChannel);
    }

    if (!stateInitialized || replayActive != lastReplayActive) {
        renderReplay(replayActive);
    }

    uint32_t now = millis();
    bool batteryIntervalElapsed = !lastBatteryUpdateMs || now - lastBatteryUpdateMs >= BATTERY_UPDATE_INTERVAL_MS;
    bool batteryChanged =
        lastBatteryMillivolts < 0 ||
        batteryMillivolts < 0 ||
        abs(batteryMillivolts - lastBatteryMillivolts) >= BATTERY_UPDATE_THRESHOLD_MV;

    if (!stateInitialized || (batteryIntervalElapsed && batteryChanged)) {
        renderBattery(batteryMillivolts);
    }

    lastBleConnected = bleConnected;
    lastBleAdvertising = bleAdvertising;
    lastWifiScanning = wifiScanning;
    lastWifiChannel = wifiChannel;
    strncpy(lastTargetLabel, safeTarget, sizeof(lastTargetLabel) - 1);
    lastTargetLabel[sizeof(lastTargetLabel) - 1] = 0;
    lastReplayActive = replayActive;
    stateInitialized = true;
}

/**
 * Renders the current BLE connection state in large text.
 */
void DisplayStatus::renderConnection(bool bleConnected, bool bleAdvertising)
{
    gfx->fillRect(STATUS_X, BLE_Y, gfx->width() - STATUS_X, LARGE_ROW_HEIGHT, COLOR_BLACK);
    gfx->setTextSize(2);
    gfx->setCursor(STATUS_X, BLE_Y);
    gfx->setTextColor(COLOR_WHITE);
    gfx->print("BLE ");

    if (bleConnected) {
        gfx->setTextColor(COLOR_GREEN);
        gfx->println("CONNECTED");
    }
    else if (bleAdvertising) {
        gfx->setTextColor(COLOR_GREEN);
        gfx->println("READY");
    }
    else {
        gfx->setTextColor(COLOR_YELLOW);
        gfx->println("STARTING");
    }
}

/**
 * Renders whether discovery covers all networks or one selected network.
 */
void DisplayStatus::renderTarget(bool wifiScanning, const char *targetLabel)
{
    gfx->fillRect(STATUS_X, TARGET_Y, gfx->width() - STATUS_X, LARGE_ROW_HEIGHT, COLOR_BLACK);
    gfx->setTextSize(2);
    gfx->setCursor(STATUS_X, TARGET_Y);
    gfx->setTextColor(COLOR_WHITE);

    if (!wifiScanning) {
        gfx->println("WIFI IDLE");
        return;
    }

    gfx->print("NET ");
    char label[17] = {};
    strncpy(label, targetLabel, sizeof(label) - 1);
    gfx->println(label);
}

/**
 * Renders the hopping or selected Wi-Fi channel from an off-screen buffer.
 */
void DisplayStatus::renderChannel(bool wifiScanning, int wifiChannel)
{
    channelCanvas->fillScreen(COLOR_BLACK);
    channelCanvas->setTextWrap(false);
    channelCanvas->setTextSize(2);
    channelCanvas->setCursor(0, 0);
    channelCanvas->setTextColor(COLOR_WHITE);

    if (wifiScanning) {
        channelCanvas->print("SCAN CH ");
        channelCanvas->println(wifiChannel);
    }
    else {
        channelCanvas->println("SCAN --");
    }

    channelCanvas->flush();
}

/**
 * Shows replay activity briefly without changing command behavior.
 */
void DisplayStatus::renderReplay(bool replayActive)
{
    gfx->fillRect(STATUS_X, REPLAY_Y, gfx->width() - STATUS_X, LARGE_ROW_HEIGHT, COLOR_BLACK);
    if (!replayActive) {
        return;
    }

    gfx->setTextSize(2);
    gfx->setCursor(STATUS_X, REPLAY_Y);
    gfx->setTextColor(COLOR_YELLOW);
    gfx->println("REPLAY");
}

/**
 * Renders the complete battery row off-screen and flushes it in one transfer.
 */
void DisplayStatus::renderBattery(int batteryMillivolts)
{
    batteryCanvas->fillScreen(COLOR_BLACK);
    batteryCanvas->setTextWrap(false);
    batteryCanvas->setTextSize(2);
    batteryCanvas->setCursor(0, 0);
    batteryCanvas->setTextColor(COLOR_WHITE);
    batteryCanvas->print("BAT ");

    if (batteryMillivolts > 0) {
        batteryCanvas->print(batteryMillivolts / 1000.0f, 2);
        batteryCanvas->println("V");
    }
    else {
        batteryCanvas->println("--");
    }

    batteryCanvas->flush();
    lastBatteryMillivolts = batteryMillivolts;
    lastBatteryUpdateMs = millis();
}

#else

void DisplayStatus::begin()
{
}

void DisplayStatus::loop(
    bool bleConnected,
    bool bleAdvertising,
    bool wifiScanning,
    int wifiChannel,
    const char *targetLabel,
    bool replayActive,
    int batteryMillivolts
)
{
    (void)bleConnected;
    (void)bleAdvertising;
    (void)wifiScanning;
    (void)wifiChannel;
    (void)targetLabel;
    (void)replayActive;
    (void)batteryMillivolts;
}

#endif
