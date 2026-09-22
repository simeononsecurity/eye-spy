# Detection Methods Reference — eye-spy

This is a living reference of every detection engine the firmware
implements, so agents don't have to re-derive the full picture from
scattered code each session. **Keep this file in sync whenever a
detection engine is added, removed, or its scoring changes** (see the
self-updating meta-rule in `05-keep-rules-current.md`). It is the
eye-spy-architecture equivalent of flock-you-esp32's own
`.clinerules/04-detection-methods.md`.

All detections funnel into `g_score` via `addScore()` (`es_confidence.h`),
gated by a per-engine `DETECTION_RESCORE_MS` (120 s) cooldown so a
persistent device can't infinitely stack points. `g_score` decays −1 every
`SCORE_DECAY_INTERVAL` (60 s), but decay is suppressed ("sticky alert")
while `g_stickySeen` is within `DETECTION_RESCORE_MS` of now — i.e. while
*any* detector is still actively firing. LED/display state derives purely
from `g_score` thresholds: 0–2 GREEN (clear), 3–5 YELLOW (caution), 6+ RED
(alert) — see `SCORE_ALERT`/`SCORE_CAUTION` in `es_confidence.h`.

## BLE detections (`EyeSpyBLECallbacks::onResult()` in `main.cpp`, patterns/
tables in `es_detect.h`, scored via `CHECK_DET()` in `processBLE()`)

| Detector       | Trigger                                                                 | Points (`PTS_*`)      |
|----------------|--------------------------------------------------------------------------|------------------------|
| `axon`         | `addr` OUI `00:25:df` (Axon body camera)                                  | `PTS_AXON` = 5          |
| `rayban`       | Advertised service UUID `0xFD5F` (Ray-Ban Meta smart glasses)             | `PTS_RAYBAN` = 5        |
| `flockBle`     | Device name — substring-matches `FLOCK_BLE_NAMES` (flock/raven/penguin/pigvision/fs ext battery/**dfutarg**) **or** matches a firmware-derived name *shape* via `bleNameShapeMatch()` (`Penguin-`+10 digits, a bare 10-digit serial, `FS Ext Battery`, `DfuTarg`) | `PTS_FLOCK_BLE` = 5 |
| `flockBleMfr`  | Manufacturer-data company ID in `FLOCK_BLE_MFR_IDS[]` — `0x09C8` (XUNTONG, confirmed Flock), matched via `flockBleMfrIdMatch()` rather than an inlined literal | `PTS_FLOCK_BLE_MFR` = 5 |
| `ravenBle`     | Advertised service UUID matches one of `RAVEN_UUIDS[]` (5 **vendor-specific** services) **or falls anywhere in the Raven 16-bit range `0x3100`–`0x3500`** (see below) | `PTS_RAVEN_BLE` = 5 |
| `flockGatt`    | Advertised service UUID matches `FLOCK_GATT_UUIDS[]` — Flock accessory `e8ccbb38-9532-46a8-9fe5-1814df172e6f` or Nordic legacy DFU `00001530-1212-efde-1523-785feabcd123` | `PTS_FLOCK_GATT` = 5 |
| `skimmer`      | Device name exact-matches `SKIMMER_NAMES` (`HC-03`/`HC-05`/`HC-06`)        | `PTS_SKIMMER` = 5       |
| `airtag`       | Mfr-data `0x004C` subtype `0x12`/`0x1E`, or raw payload fallback `1E FF 4C 00` / `4C 00 12` | `PTS_AIRTAG` = 4 |
| `odidBle`      | Advertised service UUID `0xFFFA`, or raw AD payload matching OpenDroneID service-data pattern | `PTS_ODID_BLE` = 4 |
| `smarttag`     | Advertised service UUID `0xFD5A` (Samsung SmartTag)                       | `PTS_SMARTTAG` = 3      |
| `tile`         | Advertised service UUID `0xFEED` or `0xFEEC` (Tile tracker)               | `PTS_TILE` = 3          |
| `meshcore`     | Device name prefix `MeshCore-`                                           | `PTS_MESHCORE` = 2      |
| `ibeacon`      | Mfr-data `0x004C` type `0x02` len `0x15` (generic iBeacon — retail/venue tracking) | `PTS_IBEACON` = 2 |
| `persist`      | Same unclassified MAC seen ≥`PERSIST_MIN_COUNT` (3) times over ≥`PERSIST_MIN_MS` (5 min) | `PTS_PERSIST` = 2 |

BLE detections are **always standalone** — eye-spy has never had
flock-you-esp32's historical "BLE-only alert invisible" bug: `CHECK_DET()`
calls `addScore()` unconditionally for every detector flag, with no
requirement of a corroborating WiFi hit. Confirmed by direct code reading
(`es_confidence.h`).

### Firmware-derived BLE additions (Flock camera firmware dump, 2026-09-16)

Two notes worth keeping, because both are easy to get wrong later:

- **Standard Bluetooth SIG services must never count as Raven evidence.**
  `0x180A` (Device Information), `0x1809` (Health Thermometer) and `0x1819`
  (Location and Navigation) *were* listed in `RAVEN_UUIDS[]` because GainSec's
  write-up includes them (Raven fw 1.1.x advertises `0x1809`/`0x1819` as
  stand-ins for its own health/location services). But they are advertised by
  essentially every BLE device in existence, so scoring them as Raven evidence
  makes ordinary hardware — fitness bands, watches, earbuds — register as
  surveillance equipment. They now live in `RAVEN_LEGACY_UUIDS[]` as
  firmware-estimation evidence only, and `service16IsStandardSvc()` is
  consulted by `ravenServiceInRange()` so widening the range or re-adding a
  standard UUID to the table cannot reintroduce the bug. Three tests enforce it
  (`test_raven_standard_services_never_alert`,
  `test_raven_table_has_no_standard_services`,
  `test_service16_standard_classifier`). Deliberate **test-behaviour change**:
  the old `test_raven_uuids_count_and_contents` asserted 8 entries including
  `0x180A`; it now asserts 5 vendor-only entries.
- **The Raven `0x3100`–`0x3500` range sweep exists because the named table was
  not enough.** `RAVEN_UUIDS[]` only holds the round-hundred services
  (`0x3100`, `0x3200`, …). The camera also advertises services across the whole
  range, and the ones that actually leak GPS — `0x3101`/`0x3102` — are *not* in
  that table, so exact-string matching silently missed the highest-value
  services. `ravenUuidInRange()` (via `ravenService16FromUuidString()`) parses
  both the canonical 128-bit form `NimBLEUUID::toString()` emits and the short
  `0x3101`/`3101` forms. The 16-bit value is the **low** half of the first
  32-bit group — `00003101-…` is `0x3101`, not `0x0000` (flock-you-esp32's
  Python equivalent had exactly that bug).
- **The Flock accessory service is deliberately *not* in `RAVEN_UUIDS[]`.** It
  is Flock's own GATT service, not a Raven one; folding it in would report it
  as `Raven-BLE-UUID` and mislabel it everywhere. It gets `FLOCK_GATT_UUIDS[]`
  and the `flockGatt` detector instead.
- `onResult()` stays **flag-only** — no `Serial.print` — per the callback
  rule in `01-clean-code.md`. The `flockGatt`/`ravenBle` logs come from
  `CHECK_DET()` in loop context, rate-limited by `DETECTION_RESCORE_MS`.

## WiFi active-scan detections (`processWifiScan()` in `main.cpp`, tables in
`es_detect.h`, scored inline via `addScore()`)

| Detector         | Trigger                                                                 | Points (`PTS_*`)     |
|------------------|--------------------------------------------------------------------------|-----------------------|
| Flock OUI        | BSSID matches `FLOCK_OUIS[]` (35 entries, synced with flock-you-esp32's `fy_oui_high[]`) | `PTS_FLOCK_OUI` = 5 |
| Flock-FW-MAC     | BSSID is **exactly** one of `FLOCK_FW_DEFAULT_MACS[]` — the two factory-default QCA9377 radio MACs from the camera firmware image (full 6-byte match) | `PTS_FW_DEFAULT_MAC` = 6 |
| ALPR OUI         | BSSID matches `ALPR_OUIS[]` (Motorola Solutions / Vigilant LPR cameras)   | `PTS_ALPR_OUI` = 5    |
| Flock SSID       | SSID contains a `FLOCK_SSID_KW` keyword (flock/flocksafety/fs ext/penguin/pigvision/raven/**flck**) | `PTS_FLOCK_SSID` = 5 |
| ALPR SSID        | SSID contains an `ALPR_SSID_KW` keyword (alpr/lpr/vigilant/plateread/…)   | `PTS_ALPR_SSID` = 4   |
| SoundThinking OUI| BSSID matches `SOUNDTHINKING_OUIS[]` (`d4:11:d6`, ShotSpotter, often co-deployed with Flock ALPR) | `PTS_SOUNDTHINKING` = 4 |
| Camera OUI       | BSSID matches `CAM_OUIS[]` (31 entries — Hikvision/Dahua/Axis/Ring/Nest/Arlo/Wyze/Reolink/FLIR/Amcrest/Vivotek/Hanwha/Mobotix/Ubiquiti) | `PTS_CAM_OUI` = 3 |
| Camera SSID      | SSID contains a `CAM_SSID_KW` keyword (cam/cctv/dvr/doorbell/nvr/…)       | `PTS_CAM_SSID` = 2    |
| Flock-mfr OUI    | BSSID matches `FLOCK_MFR_OUIS[]` (8 entries — 7 Liteon/USI contract-manufacturer OUIs incl. `14:b5:cd`, plus **`00:03:7f` Qualcomm Atheros** from the firmware dump; shared with non-Flock hardware) | `PTS_FLOCK_MFR_OUI` = 2 |

### Firmware-derived WiFi additions (Flock camera firmware dump, 2026-09-16)

- **`00:03:7f` is deliberately mfr-tier, not `FLOCK_OUIS`.** It is the QCA9377
  radio's chipset-vendor prefix, present on a huge installed base of unrelated
  Atheros gear, so the OUI alone is not evidence of a camera.
- **The two *factory-default* MACs in that block are a different story**, and
  are checked exactly (all six bytes) by `fwDefaultMacMatch()`:
  `00:03:7f:50:00:01` (`bdwlan30.bin`/`fakeboar.bin`) and
  `00:03:7f:4f:00:16` (`otp30.bin`). Those are what the radio transmits while
  still **unprovisioned**; provisioning rewrites the MAC, so this engine only
  ever fires on a freshly-imaged unit — and there is no plausible unrelated
  device transmitting from a factory default, which is why
  `PTS_FW_DEFAULT_MAC` = 6 is the **one WiFi signal that can reach
  `SCORE_ALERT` on its own**. It is checked *before* the mfr-tier test, and the
  scan `continue`s on a match so the same network isn't also scored as a
  low-confidence mfr hit.

Each of these engines uses a per-scan "seen once" boolean (`fFlockOui`,
`fCamSsid`, etc.) so a scan with multiple matching networks only scores
once per engine per scan cycle — see `processWifiScan()`.

## WiFi promiscuous detection (`wifiSniffer()` IRAM_ATTR callback in
`main.cpp`, `PHASE_PROMISC` only)

| Detector    | Trigger                                                                    | Points (`PTS_*`)   |
|-------------|-------------------------------------------------------------------------------|----------------------|
| `odidWifi`  | 802.11 management frame with dest MAC `51:6f:9a:01:00:00` (OpenDroneID/ASTM F3411 Remote ID NaN broadcast) | `PTS_ODID_WIFI` = 4 |

This is the only detection engine in the promiscuous phase — narrower in
scope than flock-you-esp32's always-on WiFi promiscuous sniffer, which
runs many OUI/SSID engines concurrently with channel-hopping.

## Phase schedule (`Phase` enum + `loop()`'s `switch` in `main.cpp`)

```
PHASE_BLE (9s, BLE_SCAN_DURATION_S)
  -> PHASE_WIFI_SCAN (kicks off WiFi.scanNetworks(async=true))
  -> PHASE_WIFI_WAIT (polls WiFi.scanComplete(); non-blocking)
  -> PHASE_PROMISC (5s, PROMISC_WINDOW_MS; channel-hops {1,6,11,3,8,13} every 400ms)
  -> back to PHASE_BLE
```

Unlike flock-you-esp32's `BLE_COEX_MODE` (a single continuous NimBLE scan
running concurrently with WiFi promiscuous sniffing via the ESP-IDF
software coexistence scheduler), eye-spy time-multiplexes: only one radio
mode is active at a time, and the three phases take turns. The
`-DENABLE_BLE_SCAN=1 -DBLE_COEX_MODE=1` build flags defined on the `*-ble`
`platformio.ini` environments are **vestigial** — `main.cpp` does not
branch on either macro, so BLE scanning runs identically regardless of
which environment is built (see `03-file-size-and-decomposition.md`).

### `startBLEScan()` — fixed blocking/async overload bug

`startBLEScan()` (`main.cpp`) starts the BLE scan for `PHASE_BLE`. It
previously called `g_pScan->start(BLE_SCAN_DURATION_S, false)`, a 2-arg
`(uint32_t, bool)` call that resolves to NimBLEScan's **blocking**
overload (returns `NimBLEScanResults`, internally
`ulTaskNotifyTake(pdTRUE, portMAX_DELAY)`), freezing `loop()` for the
full 9-second scan every BLE phase — the same overload-ambiguity bug
class already found and fixed in flock-you-esp32's `bleCoexStart()`.
Fixed by forcing the async 3-arg overload via an explicit typed null
callback: `g_pScan->start((uint32_t)BLE_SCAN_DURATION_S, (void
(*)(NimBLEScanResults))nullptr, false)`. See `01-clean-code.md` for the
full root-cause writeup.

## Test tooling that exercises these paths

- `test/test_oui_matching/` and `test/test_ssid_ble_matching/` (native/host
  Unity tests, 32 test cases total) — exercise `es_detect.h`'s pure
  matching helpers (`ouiMatch()`, `ssidHas()`, `strContainsCI()`) and
  pattern-table integrity (counts, mutual exclusivity, null-termination)
  directly on the host, no hardware required. Run via `pio test -e native`
  or the platformio-mcp `run_tests` tool.
- `src/es_beacon_test.cpp` (`[env:atom-lite-beacon]` PlatformIO
  environment, standalone `.cpp` entry point selected via
  `build_src_filter`, same pattern as flock-you-esp32's `beacon_test.cpp`)
  — the hardware-in-the-loop equivalent, covering all 21 rotating
  detection scenarios (12 BLE + 8 WiFi-AP + 1 WiFi-promiscuous) plus a
  separately-timed `persist` scenario, for testing against a SECOND board
  running a real detector environment (e.g. `atom-lite`). Unlike
  flock-you-esp32's fast ~4s rotation (viable because that detector's
  `BLE_COEX_MODE` listens continuously), eye-spy time-multiplexes BLE/WiFi-
  scan/promiscuous phases (see "Phase schedule" above) with a full rotation
  period of ~17-19s, so each scenario here is held for
  `SCENARIO_HOLD_MS` = 20 s to reliably overlap the detector's full phase
  cycle at least once, regardless of which phase it happens to be in when
  the scenario starts.
  - **BLE scenarios** (12): straightforward `NimBLEAdvertising` calls for
    `rayban`/`flockBle`/`flockBleMfr`/`ravenBle`/`skimmer`/`airtag`/
    `odidBle`/`smarttag`/`tile`/`meshcore`/`ibeacon`, plus `axon`, which
    needs the tester's *own* BLE advertising address to fall in the
    `00:25:df` OUI (since `axon` matches on `addr`, the peer address, not
    manufacturer data) — done via a manually-constructed reversed-byte-
    order address (`NimBLEAddress` stores bytes in reverse vs. the printed
    `xx:xx:xx:xx:xx:xx` string) passed to `ble_hs_id_set_rnd()`, mirroring
    `ble_selftest.h`'s address-manipulation pattern from flock-you-esp32.
  - **WiFi-AP scenarios** (8): Flock/ALPR/SoundThinking/Flock-mfr OUI and
    Flock/ALPR/Camera SSID-keyword engines all require a real, discoverable
    BSSID/SSID pair during `WiFi.scanNetworks()` — not raw frame injection
    like flock-you-esp32's promiscuous sniffer — so each scenario spins up
    a real SoftAP with a spoofed MAC via
    `WiFi.mode(WIFI_AP); esp_wifi_set_mac(WIFI_IF_AP, mac); WiFi.softAP(ssid);`
    (the documented ESP-IDF sequence: interface must not yet be broadcasting
    when `esp_wifi_set_mac()` is called; `mode(WIFI_AP)` brings up the AP
    netif without starting the beacon, satisfying that constraint).
  - **WiFi-promiscuous scenario** (1): `odidWifi` is exercised via a real
    raw-802.11 beacon frame (`es_beacon_frames.h`, a minimal builder mirroring
    flock-you-esp32's `beacon_frames.h`) with dest MAC (addr1) set to the
    OpenDroneID NaN broadcast `51:6f:9a:01:00:00`, sent via
    `esp_wifi_80211_tx()` on a channel sweep so it lands during whichever
    channel the detector's `PHASE_PROMISC` happens to be dwelling on.
  - **`persist`** is deliberately excluded from the 20 s-hold rotation and
    instead fires on its own independent `PERSIST_INTERVAL_MS` (150 s)
    timer using one fixed random BLE address generated once at boot — this
    lets 3+ sightings of the same unclassified address accumulate across
    `PERSIST_MIN_MS` (5 min) within a single test session without waiting
    for ~3 full 21-scenario rotations (~21+ minutes) if folded into the
    main shuffle.
  - **Build/flash:** `pio run -e atom-lite-beacon -t upload` (or the
    platformio-mcp `build_project`/`upload_firmware` tools with
    `environment=atom-lite-beacon`). GPIO39 button force-advances to the
    next scenario early, for faster manual testing.
  - **Both transmit paths report failures instead of swallowing them.**
    `wifiApHoldWithMac()` now checks `WiFi.softAP()`'s bool and only prints
    its `SoftAP up ...` line on success — it previously ignored the return
    value *and* unconditionally claimed success, so "the detector missed my
    SoftAP" and "my SoftAP never came up" were indistinguishable from the
    serial log alone. `scenarioOdidWifi()` now counts every failed
    `esp_wifi_80211_tx()`/`esp_wifi_set_channel()` call and prints one
    cumulative `[esbeacon] WARN tx failed ...` line per scenario (per-frame
    logging would flood a 20 s hold loop). **Check those WARN lines before
    concluding a scenario exposed a detector bug** — they are the definitive
    "did the frames actually go out over the air" answer, and without them a
    tester-side TX refusal looks exactly like a detector-side miss. This
    mirrors flock-you-esp32's `txSweep()` fix for the identical ambiguity.
