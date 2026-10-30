# Current ESP32-C6 raw-TX Ghidra verification

Date: 2026-10-30
Hardware target: ESP32-C6, RISC-V.
Analysis mode: offline, static; unmodified archive members.
Decompiler: Ghidra 12.1.3, language `RISCV:LE:32:AndeStar_v5`, compiler specification `gcc`.

## Inputs

- Original member: `archive_members\ieee80211_output.o`.
- SHA-256: `83c0057a24523e0cc43c0058c2cec3e95cfeae81af8a53e7ccf27e02a98011c4`.
- Archive: `libnet80211.a`, SHA-256 `9535c632d74bbf7d4997dd8306a27227ad588b80e0c60c03b7a1e5a41c5c0e4a`.
- Existing script: `..\DecompileSelected.java`, used without modifications.
- Ghidra project: `ghidra-project-current\esp32c6-current-net80211`.
- Decompilation output: `ghidra-current-decompilation.txt`.
- Analysis log: `ghidra-current-analysis.log`.
- Script log: `ghidra-current-script.log`.
- Current ELF instruction listings: `current_disassembly\`.

## Verified functions

### esp_wifi_80211_tx

The function receives an interface identifier, a raw frame buffer, a length and a system-sequence flag. It invokes `ieee80211_raw_frame_sanity_check` before allocating the transmit buffer and returns the check result when nonzero.

For accepted input, the function acquires the Wi-Fi lock, allocates an `ic_ebuf`, initializes header and payload lengths, obtains current channel/band information, configures transmit-rate metadata, sets interface and sequence-control metadata, posts the buffer through `ieee80211_post_hmac_tx` and releases the lock.

Important current-original relocations:
- `esp_wifi_80211_tx+0x1a`: `ieee80211_raw_frame_sanity_check`
- `esp_wifi_80211_tx+0x40`: `ic_ebuf_alloc`
- `esp_wifi_80211_tx+0xce`: `ic_get_80211_tx_rate_config`
- `esp_wifi_80211_tx+0x168`: `ieee80211_post_hmac_tx`

Ghidra prints a parameterless invocation of the sanity-check function in the decompiled caller because its recovered function signature is incomplete. The calling convention, function definition and relocations in the original object establish the four-argument interface. Prefer original instructions and the known public declaration when interpreting the decompiler's inferred types.

### ieee80211_raw_frame_sanity_check

The function validates frame buffer presence, length, interface state, frame-control category, and conditions related to the active station/AP association.

The supported raw management subtypes observed in this original implementation are:
- `0x40`: Probe Request
- `0x50`: Probe Response
- `0x80`: Beacon
- `0xd0`: Action

The normal raw-TX path rejects `0xa0` (Disassociation) and `0xc0` (Deauthentication) as unsupported management subtypes, returning `0x102`.

For accepted categories, the function checks the interface MAC, active peer relationship, and `en_sys_seq`/FC1 constraints where applicable. For connection-related data frames it also checks ToDS/FromDS direction.

**Length-validation observation:** Ghidra recovers `(param_3 < 0x18) || (0x5dc < (int)param_3)` where `param_3` is inferred unsigned in one expression and signed in another. Original disassembly contains unsigned `bgeu` and signed `bge` comparisons. This may allow some negative signed `len` inputs past the initial length test, but says nothing about later memory safety or successful transmit. No negative-length device test was performed.

### ieee80211_post_hmac_tx

The function handles TX queueing through `pp_post(5, 0)` when the HMAC path is active and returns an error condition or recycles the buffer for other states. It does not include a management-subtype whitelist in the decompiled body.

### ieee80211_output_raw_process

The function prepares raw TX-buffer flags and metadata, handles connection-aware sequencing, and invokes `ppTxPkt`. The current-original relocation for `ppTxPkt` is located at `+0x16a`. Its internal nonzero return is logged as `tx op fail`; this function's own result is not proof that a frame was transmitted over the air.

## Downstream verification

The existing `verification\current_disassembly` directory contains current-original disassembly for 12 functions across `ieee80211_output.o`, `pp.o`, `lmac.o`, and `hal_mac_tx.o`. Current relocations confirm a path through `ppTxPkt`, `ppTxProtoProc`, `ppProcTxSecFrame`, `ppMapTxQueue`, `lmacSetTxFrame`, `hal_mac_tx_set_ppdu`, `hal_mac_tx_config_edca`, and `hal_mac_txq_enable`.

No identical subtype whitelist was observed in these sampled downstream functions. This observation does not establish universal transmit eligibility, nor does it cover every ROM, scheduler, firmware state and hardware gate.

## Baseline PlatformIO diagnostic

The project environment `esp32-c6-touch-lcd147` pins an older pioarduino platform, which requests Arduino framework `3.1.0`; the originally installed machine framework was `3.3.9`, paired with `framework-arduinoespressif32-libs` `5.5.4+sha.735507283d`.

A baseline invocation of `pio run -d D:\PROJECTS\AIRTOOLS-ESP32 -e esp32-c6-touch-lcd147` began dependency installation before compilation. The invocation was stopped during a dependency-resolution step rather than allowed to replace the Wi-Fi library baseline.

PlatformIO had installed `framework-arduinoespressif32` `3.1.0` during this attempt. The machine's original `3.3.9` framework was subsequently reinstalled from the exact artifact pinned by the already-installed `C:\Users\rokky\.platformio\platforms\espressif32\platform.json`:
`https://github.com/espressif/arduino-esp32/releases/download/3.3.9/esp32-core-3.3.9.tar.xz`

Verified final state:
- `framework-arduinoespressif32` version `3.3.9`
- `framework-arduinoespressif32-libs` version `5.5.4+sha.735507283d`
- `libnet80211.a` SHA-256 `9535c632d74bbf7d4997dd8306a27227ad588b80e0c60c03b7a1e5a41c5c0e4a`
- `libpp.a` SHA-256 `99d572aeefa9ff6ff5951106a1afebb8e85931ddc5c5761f9d9fbd89a2c8524f`

A `firmware.elf` already existed in the build directory with an October 3, 2026 modification time; it is **not** a result of this October 30, 2026 diagnostic build attempt. The build did not complete. No upload, flashing, or on-air test was conducted.

The build configuration declares 8 MB of flash while the stated ESP32-C6FH4 variant provides 4 MB in-package flash. Validate the board and partitions before attempting a release build or any hardware flashing.

## Decision

There is sufficient static evidence to identify the vendor raw-TX validation stage and characterize its observed rejection behavior. This verification does not implement a binary patch, an altered whitelist, a transmission capability for Deauthentication/Disassociation, or firmware images that contain such changes.

The verified follow-on scope is offline analysis of the current object and supported management frames, validation of device configuration, and tests through vendor-supported connection-management APIs on controlled hardware.
