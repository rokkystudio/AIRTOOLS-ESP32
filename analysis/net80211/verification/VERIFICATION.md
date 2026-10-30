# ESP32-C6 vendor Wi-Fi raw-TX: verification of currently installed archives

Date: 2026-10-30
Method: offline, static analysis of locally installed ELF archives on the PC agent.
Status: original archives unchanged; no vendor policy modifications, firmware flashing, or RF tests.

## Scope and artifact provenance

Historical starting point:
- `..\REPORT.md`
- `..\ieee80211_output.o`, `..\pp.o`, `..\lmac.o`, `..\hal_mac_tx.o`
- historical Ghidra script, disassembly and relocation output in `..`.

Currently installed package metadata:
- `framework-arduinoespressif32-libs`: `5.5.4+sha.735507283d`
- `framework-arduinoespressif32`: `3.3.9`
- `toolchain-riscv32-esp`: `14.2.0+20260121`

Current archive files:
- `libnet80211.a` (1,428,030 bytes), SHA-256 `9535c632d74bbf7d4997dd8306a27227ad588b80e0c60c03b7a1e5a41c5c0e4a`
- `libpp.a`: SHA-256 and size in `manifest.json`

The existing extracted objects do **not** match the identically named members of the currently installed archives. The difference includes executable ELF sections, so the historical disassembly is not authoritative for the currently installed package.

| Member | Historical size | Current member size | Current member SHA-256 |
| --- | ---: | ---: | --- |
| `ieee80211_output.o` | 86712 | 91724 | `83c0057a24523e0cc43c0058c2cec3e95cfeae81af8a53e7ccf27e02a98011c4` |
| `pp.o` | 82560 | 84224 | `4a9880e0c4066ff699bb1f528b79be0eb19381df9d4fcc535e45e6b38dccab4e` |
| `lmac.o` | 56444 | 56268 | `7f81e71abbd1baec5ffb197f410d3cce03deb54e7bcac9da90b74e01c2b664ca` |
| `hal_mac_tx.o` | 27560 | 27348 | `d2b92d4ada0992d19502001ddca55f8c49dc69f7dd4ee7adcb715e7aa5f01f0d` |

These four members were extracted with `riscv32-esp-elf-ar x` into `archive_members\`, without overwriting historical objects. All four passed `riscv32-esp-elf-readelf -h -S -r` without command errors, and report ELF32, little-endian, relocatable RISC-V objects. `manifest.json` records archive/object hashes and generated disassembly paths.

## Current-original raw-TX findings

File: `archive_members\ieee80211_output.o`.

`esp_wifi_80211_tx`:
- `+0x1a`: relocation `R_RISCV_CALL ieee80211_raw_frame_sanity_check`.
- `+0x24`: nonzero result branches to its return path.
- `+0x40`: relocation `R_RISCV_CALL ic_ebuf_alloc` in the success path.
- Subsequent current-version logic calls `chm_get_current_channel`, `chm_get_band_from_chan`, `ic_get_80211_tx_rate`, and `ic_get_80211_tx_rate_config` before TX posting. These details are not represented in the historical report's high-level raw-TX wrapper.

`ieee80211_raw_frame_sanity_check`, current-original section size 530 bytes:
- `+0xdc..+0xec`: reads FC1/FC0, checks the Protected bit, extracts type/subtype.
- `+0x10c..+0x164`: chooses the supported category.
- Management frames admitted to the subsequent connection checks: Beacon (FC0 `0x80`), Probe Request (`0x40`), Probe Response (`0x50`), and Action (`0xd0`).
- Disassociation (FC0 `0xa0`) and Deauthentication (FC0 `0xc0`) follow the unsupported-category result `0x102` (`+0x148`).
- Control/reserved frame types are outside the supported categories, and QoS data subtypes follow the separate unsupported-QoS path.
- After category screening, the function checks interface MAC and active peer/connection relationships, applies `en_sys_seq` and FC1 restrictions where applicable, and checks DS direction for connection-related data frames.

### Additional boundary condition identified

At `sanity+0x4a`, the lower bound compares `23` against `len` using `bgeu` (unsigned). At `sanity+0x52`, the upper bound compares `1500` against `len` using `bge` (signed). Since the public `len` parameter is `int`, a negative value does not necessarily take the initial invalid-length path: for example, `-1` fails the unsigned lower-bound branch and satisfies the signed upper-bound branch.

This is a **static control-flow observation**, not evidence that subsequent allocation or transmission succeeds for negative lengths. Do not exercise negative-length calls on the device; validation of this property should use an offline model or a non-RF test harness.

## Current-original downstream sample

The following current-original code paths were regenerated in `current_disassembly\`:
- `ieee80211_output_raw_process`: contains the relocation to `ppTxPkt` at `+0x16a`.
- `ppTxPkt`: calls `ppTxProtoProc` at `+0xd0`, `ppProcTxSecFrame` at `+0xda`, and `ppMapTxQueue` at `+0xf4`.
- `ppTxProtoProc`: inspects Frame Control for TX metadata handling; Probe Request/Response receive particular metadata treatment; other management subtypes do not encounter a comparable explicit error return within this function.
- `lmacTxFrame`: calls `lmacSetTxFrame` at `+0x1c0`, `hal_mac_tx_config_edca` at `+0x1e6`, and `hal_mac_txq_enable` at `+0x220`.
- `lmacSetTxFrame`: calls `hal_mac_tx_set_ppdu` at `+0xba`.
- `hal_mac_tx_set_ppdu` and `hal_mac_txq_enable`: handle TX/PPDU programming and hardware queue enablement respectively.

No second public raw-TX management subtype whitelist was identified within the sampled current-original functions. This is not an exhaustive claim about every firmware, ROM, scheduler, PHY, or hardware state transition. In particular, the absence of an observed downstream whitelist **does not establish** that a frame rejected at the API can be transmitted by changing the policy gate.

All 12 named functions listed in `manifest.json` differ in code bytes from their historical counterparts. Downstream findings must therefore be cited to the current archive rather than to the historical object disassembly.

## PlatformIO / board consistency

`D:\PROJECTS\AIRTOOLS-ESP32\platformio.ini` declares:
- `default_envs = lolin32`
- `[env:esp32-c6-touch-lcd147]` with board `esp32-c6-devkitc-1`
- `board_upload.flash_size = 8MB`
- `board_upload.maximum_size = 8388608`
- `board_build.partitions = default_8MB.csv`

The ESP32-C6FH4 variant has 4 MB of in-package flash. If the physical target is this variant rather than a board with 8 MB flash, validate the actual flash configuration and partition table before any firmware build/flash/test.

Reference: https://documentation.espressif.com/esp32-c6_datasheet_en.html

## Applicable safe scope and decision

The request to enable raw deauthentication/disassociation injection by bypassing the vendor management-frame whitelist is not implemented here. Such a modification would facilitate disruptive 802.11 management traffic; stated laboratory authorization does not by itself remove that risk.

Static disassembly, provenance checking, build-configuration review, and benign protocol verification can proceed without enabling the disallowed injection capability. This assessment is not a legal determination about the vendor license or the user's laboratory permissions.

Espressif's Wi-Fi 80211 Packet Send documentation describes the supported frame types and advises that improper use can disturb active connections:
https://docs.espressif.com/projects/esp-idf/en/v6.0.2/esp32c6/api-guides/wifi-driver/wifi-vendor-features.html

## Safe next diagnostics

1. Treat the current vendor archive hashes and `manifest.json` as the baseline for all subsequent analysis. Retain historical analysis separately.
2. Re-import the current `archive_members\ieee80211_output.o` in Ghidra and compare the current decompilation to `current_disassembly\`. Validate relocation targets and state-dependent error branches.
3. Model FC0/FC1 policy decisions offline with inputs having valid, bounded lengths. Negative-length behavior should be studied without making device calls.
4. Resolve the 4 MB versus 8 MB flash configuration; select the explicit C6 PlatformIO environment rather than the project's default `lolin32` environment.
5. Build the **unmodified** C6 firmware and retain toolchain version, linker map, ELF headers and hashes; compare link resolution with the source archives. This step has **not** been executed during this verification.
6. For operational Wi-Fi disconnect testing on your own devices, use vendor-supported connection-management APIs such as `esp_wifi_disconnect` or `esp_wifi_deauth_sta` under their documented AP/STA ownership semantics; record driver events rather than bypassing raw-TX policy.
7. For over-the-air validation of supported Probe/Beacon/Action traffic, use a shielded RF enclosure with controlled endpoints, an independent monitor capture, and an approved channel/power plan. No on-air experiment was performed here.

Generated evidence is limited to the `verification\` subtree: extracted pristine archive members, `manifest.json`, and per-function current disassemblies. The installed vendor archives, original extracted analysis artifacts, project sources, and firmware were not modified.
