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
