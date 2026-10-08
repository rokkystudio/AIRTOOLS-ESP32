# ESP32-C6 libnet80211: esp_wifi_80211_tx analysis

## Source artifact

Library:
`C:\Users\rokky\.platformio\packages\framework-arduinoespressif32-libs\esp32c6\lib\libnet80211.a`

Extracted object:
`D:\PROJECTS\AIRTOOLS-ESP32\analysis\net80211\ieee80211_output.o`

The exported symbol `esp_wifi_80211_tx` is defined by `ieee80211_output.o`.

## Generated artifacts

- `symbols.txt` — symbol table
- `relocations.txt` — ELF relocations
- `disassembly.txt` — complete object disassembly
- `esp_wifi_80211_tx.disasm.txt` — focused disassembly
- `ghidra-headless.txt` — Ghidra import/decompiler output
- `DecompileSelected.java` — headless decompiler script

## Recovered signature

The public API signature corresponding to the decompiled arguments is:

```c
esp_err_t esp_wifi_80211_tx(
    wifi_interface_t ifx,
    const void *buffer,
    int len,
    bool en_sys_seq
);
```

Ghidra recovered the function as four 32-bit ABI parameters and correctly tracked their usage.

## esp_wifi_80211_tx control flow

Recovered high-level flow:

```c
int esp_wifi_80211_tx(ifx, buffer, len, en_sys_seq)
{
    int err = ieee80211_raw_frame_sanity_check(ifx, buffer, len, en_sys_seq);
    if (err != 0) {
        return err;
    }

    lock(g_wifi_global_lock);

    eb = ic_ebuf_alloc(buffer, 1, len);
    if (eb == NULL) {
        unlock(g_wifi_global_lock);
        return 0x101;
    }

    eb->payload_len = len - 24;
    eb->header_len = 24;

    tx = eb->tx_control;
    tx->flags |= 0x4000;
    tx->scheduler = ic_get_default_sched();
    tx->rate = ic_get_80211_tx_rate(ifx);

    // Per-interface NVS-driven TX control bit.
    tx->flags2 = update_nvs_tx_flag(tx->flags2, ifx);

    if (en_sys_seq) {
        tx->flags |= 1;
    }

    tx->flags2 = set_interface_bit(tx->flags2, ifx);

    ieee80211_post_hmac_tx(eb);

    unlock(g_wifi_global_lock);
    return 0;
}
```

## Direct call graph

```text
esp_wifi_80211_tx
├─ ieee80211_raw_frame_sanity_check
│  ├─ wifi_log
│  ├─ wifi_get_macaddr
│  ├─ memcmp
│  ├─ cnx_node_search
│  └─ g_osi_funcs_p lock/unlock callbacks
├─ g_osi_funcs_p lock callback
├─ ic_ebuf_alloc
├─ ic_get_default_sched
├─ ic_get_80211_tx_rate
├─ ieee80211_post_hmac_tx
│  ├─ pp_post
│  └─ ieee80211_recycle_cache_eb
└─ g_osi_funcs_p unlock callback
```

The later asynchronous/raw-output path recovered from the same object includes:

```text
ieee80211_output_raw_process
├─ ic_ebuf_recycle_tx
├─ wifi_log
├─ hal_now
├─ wifi_get_macaddr
├─ memcmp
├─ cnx_node_search
└─ ppTxPkt
```

## Raw frame sanity check

`ieee80211_raw_frame_sanity_check` performs the policy/validation stage before allocation and TX submission.

Observed checks include:

- buffer must be non-null
- frame length must be 24..1500 bytes
- interface must be STA or AP and Wi-Fi state must exist
- protected/crypto raw frames are rejected
- QoS data variants are restricted
- management-frame subtype is filtered
- interface/source/destination relationship is checked
- when a frame could interfere with the active connection, `en_sys_seq` and frame-control bits are validated
- ToDS/FromDS values are checked for STA/AP data direction

The function returns before `ic_ebuf_alloc` when a frame fails validation.

## Important structural finding

The frame-type policy is not implemented in the thin public wrapper after allocation. It is concentrated in:

`ieee80211_raw_frame_sanity_check()`

The accepted frame then follows the ordinary internal TX-buffer path through:

`ic_ebuf_alloc -> ieee80211_post_hmac_tx -> asynchronous raw output -> ppTxPkt`

This report documents the vendor implementation for debugging and compatibility analysis; it does not modify the vendor binary.


## ieee80211_raw_frame_sanity_check: basic-block map

Recovered parameters:

```c
uint ifx;              // 0 = STA, 1 = AP
uint8_t *frame;
uint len;
bool en_sys_seq;
```

### Early validation

| Condition | Result |
|---|---|
| `frame == NULL` | `0x102` |
| `len < 24` | `0x102` |
| `len > 1500` | `0x102` |
| `ifx` not STA/AP | `0x3004` |
| per-interface `g_ic` state absent | `0x3004` |

### Frame-control decoding

The function reads the first two frame-control bytes and derives:

```c
type_bits    = frame[0] & 0x0c;
subtype_bits = frame[0] & 0xf0;
protected    = frame[1] & 0x40;
flags        = frame[1] & 0x3c;
ds_bits      = frame[1] & 0x03;
```

A protected/crypto raw frame is rejected with `0x102`.

### Accepted raw categories observed in the vendor check

For management frames (`type_bits == 0`), the sanity check reaches the next validation stage only for:

| FC0 subtype pattern | 802.11 category |
|---|---|
| `0x80` | Beacon |
| `0x40/0x50` family selected by `(FC0 & 0xe0) == 0x40` | Probe request / probe response |
| `0xd0` | Action |

Other management subtypes take the generic `"unsupport frame type: %x%x"` rejection path and return `0x102`.

For data frames (`type_bits == 8`), non-QoS data is allowed into the next stage. QoS-family data identified by the signed subtype test follows `"unsupport QoS frame type: %x%x"` and returns `0x102`.

### Connection-aware checks

After the frame category check, the function obtains the interface MAC with `wifi_get_macaddr()` and compares it with Address 2 (`frame + 10`).

For STA mode it checks the currently associated peer against Address 1 (`frame + 4`).
For AP mode it calls `cnx_node_search(frame + 4)`.

If the frame does not correspond to the active connection, the routine returns success without applying the additional connection-preservation restrictions.

If it does correspond to the active connection and Address 2 equals the interface MAC:

1. `en_sys_seq` must be true; otherwise the diagnostic is:
   `"en_sys_seq should be true to avoid side-effect to WiFi connection"`
2. Retry/power-management/more-data/protected-related checked bits represented by `frame[1] & 0x3c` must be clear.
3. For data frames:
   - STA expects `ToDS=1, FromDS=0`
   - AP expects `ToDS=0, FromDS=1`

Violations return `0x102`.

### Return codes observed

| Code | Meaning in this function |
|---|---|
| `0x0000` | sanity check accepted |
| `0x0102` | invalid argument / unsupported frame-control combination |
| `0x3004` | invalid interface or unavailable Wi-Fi interface state |

### Control-flow summary

```text
entry
├─ buffer/length validation
├─ acquire Wi-Fi global lock
├─ interface/state validation
├─ decode FC type/subtype/protected bits
├─ supported raw-frame category check
│  └─ reject -> 0x102
├─ fetch local interface MAC
├─ inspect active connection / peer
├─ if unrelated to active connection
│  └─ success -> 0
├─ if related to active connection
│  ├─ require en_sys_seq
│  ├─ require safe FC flags
│  └─ validate ToDS/FromDS for data
└─ release lock -> 0 or error
```

The policy gate therefore resides entirely before `ic_ebuf_alloc()`; only a frame that returns zero from this function reaches the raw TX buffer allocation path.


## Downstream filter search

The raw TX path was traced beyond `ieee80211_raw_frame_sanity_check()` to determine whether management subtype policy is enforced again later.

### ieee80211_post_hmac_tx

This function only queues the prepared TX buffer through `pp_post(5, 0)` or recycles it when the HMAC path is stopped. No frame-control subtype whitelist was observed.

### ieee80211_output_raw_process

This stage:

- validates that the selected interface still exists
- prepares lower-layer TX metadata
- stamps timing fields
- handles sequence-number bookkeeping
- checks active peer/connection state
- submits the frame with `ppTxPkt()`

No management subtype whitelist comparable to `ieee80211_raw_frame_sanity_check()` was observed.

### ppTxPkt (libpp.a: pp.o)

`ppTxPkt` checks interface enablement and consistency of internal TX classification metadata, then runs:

```text
ppTxProtoProc
ppProcTxSecFrame
rcGetSched
ppMapTxQueue
...
```

The checks visible in `ppTxPkt` concern internal queue/type classification rather than a public raw-frame subtype whitelist.

### ppTxProtoProc

This function reads the 802.11 frame control and adjusts TX metadata.

Observed behavior:

- data frames set data-related TX flags
- probe response subtype (`0x50`) sets a dedicated TX metadata bit
- probe request subtype (`0x40`) sets another dedicated TX metadata bit
- other management subtypes fall through without an `unsupported subtype` error path

No return/error mechanism exists in this function; it only mutates TX control metadata and returns.

### Current conclusion

Within the analyzed C6 vendor objects:

```text
esp_wifi_80211_tx
└─ ieee80211_raw_frame_sanity_check   <-- public raw-TX policy gate
   (accepted)
   ↓
ic_ebuf_alloc
↓
ieee80211_post_hmac_tx
↓
ieee80211_output_process
↓
ieee80211_output_raw_process
↓
ppTxPkt
├─ ppTxProtoProc
├─ ppProcTxSecFrame
├─ rcGetSched
└─ ppMapTxQueue
```

The management-frame subtype whitelist found so far is concentrated in `ieee80211_raw_frame_sanity_check()`. No equivalent downstream rejection of unsupported management subtypes has been identified in `ieee80211_output_raw_process`, `ppTxPkt`, or `ppTxProtoProc`.

This is an implementation map only; the vendor binary has not been modified.


## Lower-MAC path continuation

### ppProcTxSecFrame

`ppProcTxSecFrame()` performs security-related TX preparation and metadata adjustment. It inspects internal TX control bits, security state and buffer layout, and may recycle/reformat buffers.

No management-subtype whitelist comparable to `ieee80211_raw_frame_sanity_check()` was observed.

### ppMapTxQueue

`ppMapTxQueue()` maps the packet to the appropriate internal TX queue and integrates power-management/TWT/mesh state.

Observed behavior includes:

- queue selection
- power-management callbacks
- TWT force-TX handling
- mesh-related handling
- queue metadata updates

The function can return nonzero to indicate alternate handling/defer/queue behavior, but it does not implement a management-frame subtype whitelist.

### ppTxPkt final path

After protocol/security preparation and queue mapping, `ppTxPkt()`:

1. checks the interface is enabled
2. validates internal TX classification metadata
3. runs `ppTxProtoProc()`
4. runs `ppProcTxSecFrame()`
5. applies rate-control scheduling via `rcGetSched()`
6. maps the TX queue with `ppMapTxQueue()`
7. inserts the buffer into the lower-MAC `pTxRx` queue structures
8. may trigger lower-MAC processing via `pp_post()`

The visible lower path contains internal consistency and scheduling checks, not another raw management-subtype policy gate.

### Current filter location conclusion

Across the traced chain:

```text
esp_wifi_80211_tx
└─ ieee80211_raw_frame_sanity_check   <-- subtype policy / public raw-TX validation
   ↓
ic_ebuf_alloc
↓
ieee80211_post_hmac_tx
↓
ieee80211_output_process
↓
ieee80211_output_raw_process
↓
ppTxPkt
├─ ppTxProtoProc
├─ ppProcTxSecFrame
├─ rcGetSched
└─ ppMapTxQueue
   ↓
pTxRx lower-MAC queue
```

No second management-subtype whitelist has been identified below `ieee80211_raw_frame_sanity_check()` in the analyzed `libnet80211.a` and `libpp.a` objects.

This remains a diagnostic reverse-engineering result only; no vendor binary or policy check has been modified.


## LMAC / hardware-submit continuation

### pTxRx consumers

The lower queue is consumed by LMAC code in `libpp.a:lmac.o`.

Relevant functions recovered:

```text
lmacGetTxFrame
lmacSetTxFrame
lmacTxFrame
```

### lmacTxFrame

`lmacTxFrame()` consumes a queued TX buffer, applies retry/long-frame/FTM-related metadata, configures EDCA, marks the internal queue state active, and finally calls:

```text
hal_mac_txq_enable()
```

No management subtype whitelist was observed.

### lmacSetTxFrame

`lmacSetTxFrame()` performs final per-queue/hardware preparation:

- TX timeout calculation
- TXOP handling
- lifetime checks
- mesh timing adjustments
- queue ownership/state checks

The key hardware preparation call is:

```text
hal_mac_tx_set_ppdu()
```

Again, no raw management-subtype whitelist comparable to `ieee80211_raw_frame_sanity_check()` was observed.

### Effective submit chain

```text
esp_wifi_80211_tx
↓
ieee80211_raw_frame_sanity_check   <-- public subtype/policy filter
↓
ic_ebuf_alloc
↓
ieee80211_post_hmac_tx
↓
ieee80211_output_process
↓
ieee80211_output_raw_process
↓
ppTxPkt
↓
pTxRx queue
↓
lmacTxFrame
↓
lmacSetTxFrame
├─ hal_mac_tx_config_timeout
└─ hal_mac_tx_set_ppdu
↓
hal_mac_tx_config_edca
↓
hal_mac_txq_enable
↓
hardware MAC queue
```

### Drop/failure classes seen below the sanity check

The lower path does contain reasons a packet may fail or be delayed, but these are operational rather than subtype-policy checks:

- interface disabled
- queue/classification inconsistency
- security processing failure
- TX queue mapping/defer
- packet lifetime expiry
- retry/ACK/CTS timeout
- TXOP availability
- queue busy/state mismatch
- hardware TX error

No second management-frame subtype whitelist has been identified in the traced PP/LMAC path.

This analysis documents control flow and failure classes only; no driver checks or vendor binaries were modified.


## HAL / hardware queue continuation

### hal_mac_tx_set_ppdu

`hal_mac_tx_set_ppdu()` prepares the physical-layer transmit descriptor and related hardware registers.

Observed responsibilities include:

- PLCP field generation
- legacy/HT/HE signal selection
- frame length programming
- TXOP programming
- RTS-rate selection
- PTI programming
- rate-dependent register setup

The function contains consistency/logging checks on internal PPDU metadata, but no 802.11 management subtype whitelist was observed.

### hal_mac_tx_config_edca

`hal_mac_tx_config_edca()` writes EDCA/backoff-related queue configuration and interface-related bits to MAC registers. No management subtype inspection was observed.

### hal_mac_txq_enable

`hal_mac_txq_enable()` performs the final queue enable operation. It:

- writes queue-enable control bits
- reads queue/access metadata
- applies HE/TB-related bookkeeping when relevant
- updates TX statistics hooks

No inspection of raw 802.11 management subtype was observed.

### Final traced path

```text
esp_wifi_80211_tx
↓
ieee80211_raw_frame_sanity_check  <-- only observed public raw subtype policy gate
↓
...
ppTxPkt
↓
lmacTxFrame
↓
lmacSetTxFrame
↓
hal_mac_tx_set_ppdu
↓
hal_mac_tx_config_edca
↓
hal_mac_txq_enable
↓
hardware MAC TX queue
```

Across the traced net80211 -> PP -> LMAC -> HAL path, no second management subtype whitelist has been identified.

Below the sanity check, observed checks are operational/metadata checks: interface state, queue validity, lifetime, security preparation, PPDU consistency, rate/scheduler setup, EDCA/TXOP, and hardware queue state.

This analysis remains diagnostic only; no binary or policy gate has been modified.


## Exact FC0/FC1 decision matrix for ieee80211_raw_frame_sanity_check

The diagnostic strings were recovered directly from `ieee80211_output.o`:

```text
invalid buffer
invalid buffer length: %d
invalid interface %d
invalid frame control, unsupport crypto frame
unsupport frame type: %x%x
unsupport QoS frame type: %x%x
en_sys_seq should be true to avoid side-effect to WiFi connection
invalid frame control, retry/power/frag/more data bit should not set
invalid frame control, sta->ap ToDS should be 1, FromDS should be 0
invalid frame control, ap->sta ToDS should be 0, FromDS should be 1
```

### FC0 decoding

```c
type_bits    = FC0 & 0x0c;
subtype_bits = FC0 & 0xf0;
```

#### Management frames: `type_bits == 0x00`

| FC0 | Subtype | Initial policy result |
|---:|---|---|
| `0x40` | Probe Request | passes subtype gate |
| `0x50` | Probe Response | passes subtype gate |
| `0x80` | Beacon | passes subtype gate |
| `0xd0` | Action | passes subtype gate |
| all other management subtype values | other management | reject: `unsupport frame type`, return `0x102` |

The probe family is implemented as `(FC0 & 0xe0) == 0x40`, which admits both `0x40` and `0x50`.

#### Control frames: `type_bits == 0x04`

All control-frame subtypes take the generic unsupported-frame path and return `0x102`.

#### Data frames: `type_bits == 0x08`

The function performs a signed test on `subtype_bits`.

- subtype values `0x00..0x70` pass the initial type/subtype gate
- subtype values `0x80..0xf0` take `unsupport QoS frame type` and return `0x102`

Thus the initial gate admits the non-QoS half of the data subtype space and rejects the QoS-family half.

#### Reserved frame type: `type_bits == 0x0c`

Rejected by the generic `unsupport frame type` path with `0x102`.

### FC1 checks

```c
to_from_ds       = FC1 & 0x03;
more_frag_retry_power_more_data = FC1 & 0x3c;
protected_frame  = FC1 & 0x40;
```

#### Protected bit

If `FC1 & 0x40 != 0`, the function logs:

```text
invalid frame control, unsupport crypto frame
```

and returns `0x102`.

This happens before the subtype whitelist.

### Connection-aware branch

After the initial subtype gate, the function fetches the interface MAC and checks whether the frame addresses correspond to the currently active STA/AP connection.

If the frame is not recognized as belonging to the active connection, the function releases the lock and returns success without applying the following connection-preservation restrictions.

When it is recognized as connection-related and Address 2 equals the local interface MAC:

#### en_sys_seq

`en_sys_seq` must be true. Otherwise:

```text
en_sys_seq should be true to avoid side-effect to WiFi connection
```

Return: `0x102`.

#### FC1 bits 2..5

For the connection-related case:

```c
(FC1 & 0x3c) == 0
```

is required.

Otherwise:

```text
invalid frame control, retry/power/frag/more data bit should not set
```

Return: `0x102`.

The checked bits are the frame-control flags represented by mask `0x3c`: More Fragments, Retry, Power Management, and More Data.

#### DS-direction check for data frames only

If the accepted frame is not a data frame, the function returns success after the checks above.

For data frames, it additionally validates `FC1 & 0x03`:

| Interface | Required DS bits | Meaning |
|---|---:|---|
| STA (`ifx=0`) | `01b` | ToDS=1, FromDS=0 |
| AP (`ifx=1`) | `10b` | ToDS=0, FromDS=1 |

STA failure string:

```text
invalid frame control, sta->ap ToDS should be 1, FromDS should be 0
```

AP failure string:

```text
invalid frame control, ap->sta ToDS should be 0, FromDS should be 1
```

Both return `0x102`.

### Compact decision tree

```text
buffer null? ------------------------------ yes -> 0x102
length outside 24..1500? ------------------ yes -> 0x102
invalid/unavailable interface? ------------ yes -> 0x3004
FC1 Protected bit set? -------------------- yes -> 0x102
|
+-- Management?
|   +-- Probe Req/Resp, Beacon, Action? ---- no  -> 0x102
|   +-- yes -> address/connection checks
|
+-- Data?
|   +-- subtype >= 0x80? ------------------ yes -> 0x102 (QoS)
|   +-- no  -> address/connection checks
|
+-- Control/reserved ----------------------------- 0x102
|
address/peer not active-connection related? ------ 0
local Address 2 mismatch? ------------------------ 0
en_sys_seq == false? ----------------------------- 0x102
FC1 & 0x3c != 0? --------------------------------- 0x102
non-data frame? ---------------------------------- 0
STA and DS != 01? -------------------------------- 0x102
AP  and DS != 10? -------------------------------- 0x102
otherwise ---------------------------------------- 0
```

This matrix documents the observed vendor validation behavior. It does not alter or bypass the validation.
