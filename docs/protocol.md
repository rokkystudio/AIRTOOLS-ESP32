# BLE Protocol

AIRTOOLS-ESP32 uses text commands over BLE with the same command names as the legacy TCP API.

| Item | UUID |
|---|---|
| Service | 7b459c40-6a5a-4e4c-9ec7-a17001500001 |
| RX command characteristic | 7b459c41-6a5a-4e4c-9ec7-a17001500001 |
| TX response characteristic | 7b459c42-6a5a-4e4c-9ec7-a17001500001 |

The Android client writes each UTF-8 command to RX. Firmware sends response chunks through TX notifications, followed by the response terminator:

```text
--airtools-eof--
```

## Network list

The `/networks` command returns an explicit SSID encoding marker in firmware v0.2.2 and later:

```text
OK networks ssid_encoding=hex
# bssid,channel,signal_dbm,beacons,probes,data,ssid_hex
00:11:22:33:44:55,6,-58,12,0,0,416972746F6F6C73
```

The final field is the network name in uppercase hexadecimal, with two digits per stored SSID byte. An empty field denotes an unknown or hidden SSID. The Android client decodes valid UTF-8 text strictly and displays `SSID 0x...` for invalid UTF-8 or control-character names instead of introducing replacement characters.

Android client v1.14 supports both the explicit `ssid_encoding=hex` format and the older text-based `OK networks` format. Firmware v0.2.2 should be used with Android client v1.14 or later.

## Device status

The `/status` header includes `transport=ble`, `firmware=0.2.2`, and `version=0.2.2`, followed by the current capture mode, channel, and optional target BSSID.

Other command responses retain their existing command framing and EOF terminator.
