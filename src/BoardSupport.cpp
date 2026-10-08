#include "BoardSupport.h"
#include "AirtoolsConfig.h"

static const char *boardName()
{
#if AIRTOOLS_BOARD_ID == 1
    return "Lolin32";
#elif AIRTOOLS_BOARD_ID == 2
    return "ESP32 Dev Module";
#elif AIRTOOLS_BOARD_ID == 3
    return "NodeMCU-32S";
#elif AIRTOOLS_BOARD_ID == 4
    return "ESP32-S3 DevKitC-1";
#elif AIRTOOLS_BOARD_ID == 5
    return "ESP32-S3 LCD 1.47 Generic";
#elif AIRTOOLS_BOARD_ID == 6
    return "ESP32-C6 Touch LCD 1.47";
#else
    return "ESP32";
#endif
}

void BoardSupport::begin()
{
    Serial.begin(115200);
    delay(100);

#if AIRTOOLS_HAS_BATTERY_ADC
    if (AIRTOOLS_BATTERY_ADC_PIN >= 0) {
        analogReadResolution(12);
    }
#endif

    Serial.println();
    Serial.print("AIRTOOLS-ESP32 board=");
    Serial.println(boardName());
}

void BoardSupport::loop()
{
}

String BoardSupport::formatStatus() const
{
    String response;
    response += "board=";
    response += boardName();
    response += " display=";
    response += AIRTOOLS_HAS_DISPLAY;
    response += " sd=";
    response += AIRTOOLS_HAS_SD;

#if AIRTOOLS_HAS_BATTERY_ADC
    response += " battery_mv=";
    response += readBatteryMillivolts();
#else
    response += " battery_mv=unknown";
#endif

    return response;
}

/**
 * Reads the battery voltage through the configured ADC divider.
 *
 * @return Battery voltage in millivolts, or -1 when battery ADC support is disabled or invalid.
 */
int BoardSupport::readBatteryMillivolts() const
{
#if AIRTOOLS_HAS_BATTERY_ADC
    if (AIRTOOLS_BATTERY_ADC_PIN < 0 || AIRTOOLS_BATTERY_DIVIDER_DEN <= 0) {
        return -1;
    }

    int raw = analogRead(AIRTOOLS_BATTERY_ADC_PIN);
    float adcMillivolts = (raw / 4095.0f) * 3300.0f;
    return static_cast<int>(adcMillivolts * AIRTOOLS_BATTERY_DIVIDER_NUM / AIRTOOLS_BATTERY_DIVIDER_DEN);
#else
    return -1;
#endif
}
