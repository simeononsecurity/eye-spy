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
| `flockBle`     | Device name substring-matches `FLOCK_BLE_NAMES` (flock/raven/penguin/pigvision/fs ext battery) | `PTS_FLOCK_BLE` = 5 |
| `flockBleMfr`  | Manufacturer-data company ID `0x09C8` (XUNTONG, confirmed Flock)          | `PTS_FLOCK_BLE_MFR` = 5 |
| `ravenBle`     | Advertised service UUID matches one of `RAVEN_UUIDS[]` (128-bit GATT, GainSec research) | `PTS_RAVEN_BLE` = 5 |
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

## WiFi active-scan detections (`processWifiScan()` in `main.cpp`, tables in
`es_detect.h`, scored inline via `addScore()`)

| Detector         | Trigger                                                                 | Points (`PTS_*`)     |
|------------------|--------------------------------------------------------------------------|-----------------------|
| Flock OUI        | BSSID matches `FLOCK_OUIS[]` (35 entries, synced with flock-you-esp32's `fy_oui_high[]`) | `PTS_FLOCK_OUI` = 5 |
| ALPR OUI         | BSSID matches `ALPR_OUIS[]` (Motorola Solutions / Vigilant LPR cameras)   | `PTS_ALPR_OUI` = 5    |
| Flock SSID       | SSID contains a `FLOCK_SSID_KW` keyword (flock/flocksafety/fs ext/penguin/pigvision/raven) | `PTS_FLOCK_SSID` = 5 |
| ALPR SSID        | SSID contains an `ALPR_SSID_KW` keyword (alpr/lpr/vigilant/plateread/…)   | `PTS_ALPR_SSID` = 4   |
| SoundThinking OUI| BSSID matches `SOUNDTHINKING_OUIS[]` (`d4:11:d6`, ShotSpotter, often co-deployed with Flock ALPR) | `PTS_SOUNDTHINKING` = 4 |
| Camera OUI       | BSSID matches `CAM_OUIS[]` (31 entries — Hikvision/Dahua/Axis/Ring/Nest/Arlo/Wyze/Reolink/FLIR/Amcrest/Vivotek/Hanwha/Mobotix/Ubiquiti) | `PTS_CAM_OUI` = 3 |
| Camera SSID      | SSID contains a `CAM_SSID_KW` keyword (cam/cctv/dvr/doorbell/nvr/…)       | `PTS_CAM_SSID` = 2    |
| Flock-mfr OUI    | BSSID matches `FLOCK_MFR_OUIS[]` (Liteon/USI contract-manufacturer OUIs — shared with non-Flock hardware) | `PTS_FLOCK_MFR_OUI` = 2 |

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
- No beacon-test/self-test firmware equivalent exists yet (unlike
  flock-you-esp32's `beacon_test.cpp` + `ble_selftest.h`). This remains an
  open gap — see `05-keep-rules-current.md` item 3 — for anyone wanting to
  do real hardware-in-the-loop validation of the BLE/WiFi detection
  engines end-to-end rather than relying solely on the native unit tests
  plus manual field testing against real devices.
