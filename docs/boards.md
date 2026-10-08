# Board Profiles

AIRTOOLS-ESP32 uses build flags rather than hard-coded board assumptions.

## Display policy

The screen is optional. The firmware must boot, scan, advertise BLE and answer Android commands when AIRTOOLS_HAS_DISPLAY is 0. Display support reads runtime state from the same board/router objects and does not own scanner control flow.

## Adding a board

Add a new PlatformIO environment and declare only confirmed capabilities.

```ini
-D AIRTOOLS_BOARD_NAME=\"My Board\"
-D AIRTOOLS_HAS_DISPLAY=0
-D AIRTOOLS_HAS_SD=0
-D AIRTOOLS_HAS_BATTERY_ADC=1
-D AIRTOOLS_BATTERY_ADC_PIN=35
```

Do not enable display or SD until exact pins are confirmed for the physical board revision.

## ESP32-C6 Touch LCD 1.47

The `esp32-c6-touch-lcd147` environment targets the ESP32-C6 1.47-inch capacitive-touch board layout compatible with the Waveshare ESP32-C6-Touch-LCD-1.47 pinout.

Confirmed board configuration:

- ESP32-C6 with 8 MB flash
- JD9853 LCD, 172 x 320
- LCD SPI: SCK GPIO1, MOSI GPIO2, CS GPIO14, DC GPIO15, RST GPIO22, BL GPIO23
- AXS5106L touch: SDA GPIO18, SCL GPIO19, RST GPIO20, INT GPIO21
- MicroSD SPI: SCK GPIO1, MOSI GPIO2, MISO GPIO3, CS GPIO4
- Battery ADC: GPIO0 with a 200K/100K divider, so battery voltage is three times the ADC voltage

The LCD and MicroSD share GPIO1 and GPIO2. Their chip-select lines must remain mutually inactive when another SPI device owns the bus. The current firmware mounts MicroSD with the configured SPI pins; the JD9853 and AXS5106L capability flags reserve the confirmed board wiring for display and touch integration.

The C6 environment uses pioarduino platform release `53.03.10`, Arduino Core 3.1.0, NimBLE-Arduino 2.3.8 and the standard `default_8MB.csv` partition table. The pre-build include-path middleware shortens Arduino and prebuilt IDF include arguments so the Windows compiler command remains below the process command-line limit.

The current firmware initializes the JD9853-compatible panel through Arduino_GFX using the ST7789 command set and renders a minimal AIRTOOLS/BLE status screen at approximately 50% backlight. AXS5106L touch events are not consumed yet; the confirmed touch pin definitions remain reserved for a future input subsystem. SD storage and battery ADC are active in the current firmware.


### Smooth dynamic text on JD9853

Avoid clearing and redrawing a dynamic text row directly on the panel. On this controller the erase and redraw transfers can be visible as two separate states, which produces text flicker even when only a small rectangle is updated.

For smooth dynamic fields, render the complete row into an off-screen `Arduino_Canvas` and call `flush()` only after the row is complete. The canvas keeps an RGB565 framebuffer in RAM and sends the finished rectangle to the LCD as one transfer.

For slowly changing telemetry such as battery voltage, also debounce the source value before flushing. The current status screen updates the battery row no more than once per second and only when the measured voltage changes by at least 50 mV. This prevents ADC noise from causing unnecessary display transfers.

A full-screen double buffer is unnecessary for the current status UI. Small per-region canvases use much less RAM while providing the same flicker-free update behavior for dynamic text.
