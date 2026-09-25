# BLE Protocol

AIRTOOLS-ESP32 keeps command strings close to the existing TCP API but transports them over BLE.

| Item | UUID |
|---|---|
| Service | 7b459c40-6a5a-4e4c-9ec7-a17001500001 |
| RX command characteristic | 7b459c41-6a5a-4e4c-9ec7-a17001500001 |
| TX response characteristic | 7b459c42-6a5a-4e4c-9ec7-a17001500001 |

Android writes one UTF-8 command to RX. Firmware returns UTF-8 response chunks through TX notifications and ends each response with:

```text
--airtools-eof--
```

The Android side should replace AirtoolsTcpClient with a transport interface and add AirtoolsBleClient. Existing response parsers can stay close to their current shape.
