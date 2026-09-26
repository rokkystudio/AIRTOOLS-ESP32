# AIRTOOLS-ESP32

AIRTOOLS-ESP32 is a PlatformIO firmware project for ESP32-class boards. It keeps the AIRTOOLS command shape used by the Android client, while replacing MT7628/Linux control Wi-Fi with BLE and replacing Linux monitor tools with ESP-IDF Wi-Fi promiscuous capture.

## Board model

The firmware treats display, SD and battery telemetry as optional board capabilities. The scanner and BLE control path work without a screen.

| Environment | Board class | Purpose |
|---|---|---|
| lolin32 | ESP32 with battery hardware | Primary portable baseline |
| esp32dev | Generic ESP32 | Cheap fallback and lab board |
| nodemcu-32s | Generic ESP32 | Common development board |
| esp32-s3-devkitc-1 | ESP32-S3 | S3 baseline without display assumptions |
| esp32-s3-generic-lcd147 | ESP32-S3 display class | Optional ST7789/SD/PSRAM profile after pin validation |

## Commands

- /status
- /start
- /scan/start
- /scan/stop
- /networks
- /select?bssid=<mac>&channel=<n>
- /clients
- /handshakes
- /handshake/download?file=<name>.pcap
- /aireplay?mode=test&count=<n>
- /replay?station=<client-mac>

`/start` starts promiscuous capture in discovery mode or resumes capture on the selected target. Query parameters accept URL-encoded values from the Android client. `/aireplay?mode=test` reports the firmware aireplay command path and raw TX capability. `/replay?station=<client-mac>` transmits deauthentication frames spoofing the selected BSSID (broadcast when `station` is omitted) so the target station re-associates and a fresh handshake can be captured.

## Handshake PCAP

A saved handshake PCAP contains one real beacon or probe-response frame with a non-empty ESSID followed by the captured EAPOL-Key frames. Storage does not publish a completed handshake until both the management metadata and the EAPOL completion criteria are present. Management frames are queued once per discovered BSSID so normal beacon traffic cannot crowd EAPOL frames out of the bounded capture queue.

## Build

```powershell
pio run -e lolin32
pio run -e esp32-s3-devkitc-1
```

Open this folder in CLion with PlatformIO support and select the required environment.

## Limits

ESP32 promiscuous mode is useful for a portable scanner and lightweight capture tool, but it is not equivalent to MT7628 + RTL8188EUS monitor mode. AIRTOOLS-ESP32 keeps the UI/API model while using a separate ESP32 backend.

Raw frame transmission uses `esp_wifi_80211_tx`. The ESP-IDF driver documents support for beacon, probe, action and non-QoS data frames, so deauth delivery should be validated against the target board and SDK revision.
