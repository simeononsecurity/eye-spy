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
| `airtag`       | Mfr-data `0x004C` subtype `0x12`/`0x1E`, or raw payload fallback `1E FF 4C 00` / `4C 00 12` — **gated on the following window** | `PTS_TRACKER_FOLLOW` = 6, **only once following** |
| `odidBle`      | Advertised service UUID `0xFFFA`, or raw AD payload matching OpenDroneID service-data pattern | `PTS_ODID_BLE` = 4 |
| `smarttag`     | Advertised service UUID `0xFD5A` (Samsung SmartTag) — same gate as `airtag` | `PTS_TRACKER_FOLLOW` = 6, **only once following** |
| `tile`         | Advertised service UUID `0xFEED` or `0xFEEC` (Tile tracker) — same gate as `airtag` | `PTS_TRACKER_FOLLOW` = 6, **only once following** |
| `meshcore`     | Device name prefix `MeshCore-`                                           | `PTS_MESHCORE` = 2      |
| `ibeacon`      | Mfr-data `0x004C` type `0x02` len `0x15` (generic iBeacon — retail/venue tracking) | `PTS_IBEACON` = 2 |
| `persist`      | Same unclassified MAC seen ≥`PERSIST_MIN_COUNT` (3) times over ≥`PERSIST_MIN_MS` (5 min) | `PTS_PERSIST` = 2 |

BLE detections are **always standalone** — eye-spy has never had
flock-you-esp32's historical "BLE-only alert invisible" bug: `CHECK_DET()`
calls `addScore()` unconditionally for every detector flag, with no
requirement of a corroborating WiFi hit. Confirmed by direct code reading
(`es_confidence.h`). **The three trackers are the deliberate exception** — see
the next section.

### Tracker "following" gate (`airtag` / `smarttag` / `tile`)

These three engines use `CHECK_TRACKER()` rather than `CHECK_DET()`, and score
`PTS_TRACKER_FOLLOW` (6 = `SCORE_ALERT`) **only once the tracker has been
following for `TRACKER_FOLLOW_MS` (30 minutes)**. Before that, sightings are still
logged and shown as `watching (Nmin/30min)` but add **nothing** to `g_score`.

Why: trackers are the one BLE class where a single sighting carries no
information. They are carried by ordinary people, left in bags, and shipped
inside boxes, so alerting every time one goes past trains the user to ignore the
alert that actually matters. Sustained presence is the privacy event, and 30
minutes is the order of magnitude Apple and Samsung use before warning a user
about an unknown tracker travelling with them.

Design points that are easy to get wrong later:

- **Class-keyed, not MAC-keyed.** AirTags rotate their Bluetooth address roughly
  every 15 minutes while separated from their owner, so a 30-minute window keyed
  on the address could never be satisfied by a genuine follow — the exact case the
  gate exists to catch. There is one `TrackerFollowState` per tracker *class*.
- **Continuity window** (`TRACKER_CONTINUITY_MAX_GAP_MS` = 5 min): a longer gap
  resets the window, so unrelated trackers passing at 20-minute intervals can
  never accumulate into a follow. `test_scattered_sightings_never_follow` is the
  regression test for precisely that.
- **Scored once per follow, not once per window.** Re-awarding would let a tracker
  sitting on a desk ratchet `g_score` upward forever; `g_stickySeen` (refreshed on
  each sighting *while following*) is what keeps the alert alive instead. A
  non-following tracker does not refresh `g_stickySeen` either, so it cannot hold
  an unrelated existing score up while it decays.
- **Re-arms when the tracker leaves.** `trackerFollowUpdate()` sets `hits` back to
  1 on a new window, and `CHECK_TRACKER()` reads `hits == 1` as "clear the scored
  flag", so a tracker that leaves and genuinely follows again later alerts again.
  `test_hits_equals_one_marks_a_new_window` pins that cross-file contract.
- **`uint32_t` timestamps, not `unsigned long`.** On the ESP32 they are the same
  width, but on a 64-bit host `unsigned long` is 64-bit, which would silently make
  the ~49-day `millis()` wrap untestable — a wrap bug would then only appear after
  a month of uptime on the real device.
  `test_millis_wrap_does_not_break_the_window` covers it.
- `TRACKER_FOLLOW_MIN_HITS` (3) can never be the binding constraint: holding
  continuity for 30 minutes already requires ≥7 sightings. It is defence in depth,
  pinned by `test_min_hits_is_not_the_binding_constraint` so a future tweak cannot
  make it dead or contradictory.

The state machine lives in `es_detect.h` — pure, Serial-free, and taking `now` as
a parameter, so the entire 30-minute window is unit-testable in milliseconds. The
scoring and `CHECK_TRACKER()` wiring live in `es_confidence.h`.

The old per-sighting weights (`PTS_AIRTAG` = 4, `PTS_SMARTTAG` = 3,
`PTS_TILE` = 3) were **removed rather than left defined-but-unused**, so nothing
can quietly reintroduce a sighting-based tracker score.

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

## Alert presentation (chime, screen, hold)

Three output channels, deliberately kept in step so severity is readable by
ear, by eye, or by feel. **Any change to one of the three should be checked
against the other two** — they exist to express the same three levels.

| Severity | Audio (`audioAlert()` in `main.cpp`) | Screen | Core2 vibration |
|---|---|---|---|
| ALERT (≥ `SCORE_ALERT` 6) | rising 2-tone, G6 1568 Hz → C7 2093 Hz | red header, `MAC <addr>` | 2 pulses (500 ms, 150 ms gap) |
| CAUTION (≥ `SCORE_CAUTION` 3) | single tone, A5 880 Hz | amber header, `MAC <addr>` | 1 pulse (400 ms) |
| CLEAR | silent | green header | — |

- **Audio is per-board hardware, and one board has none.** `USE_M5_SPEAKER` is
  on for Atom Voice / M5Stack Basic / Core2 For AWS, `USE_BUZZER` for Atom
  Echo (G25) and StickC Plus SE (G2), and **neither** for Atom Lite,
  `esp32dev` or the T-Dongle C5. `audioAlert()` therefore compiles to a no-op
  on those — which is fine, but it means "no sound" is never by itself
  evidence of a fault, and **a guide or README must not promise sound on a
  board that cannot make any**. This exact mistake shipped once: three
  customers reported "the alert is only the vibration" because the
  M5Stack Basic/Core2 block had `USE_M5_SPEAKER 0`, and the printable guide
  flatly stated "Eye Spy is silent — no buzzer".
- **The chime must never `delay()`.** It is armed by `audioAlert()` and stepped
  by `audioAlertTick()` from the UI task, which is also what redraws the
  display. Blocking that task for the ~350 ms the chime needs is the same
  "frozen screen" failure mode the vibration tick and the display-flicker fix
  each had to remove — hence the arm/tick split and the shared
  `g_uiMux`-protected snapshot.
- **Severity is latched on the rising edge.** `ui_task.h` calls
  `audioAlert(level >= 2)` only when the level *crosses* upward, so a device
  that sits at ALERT does not re-chime every tick.

### Decaying alert/caution tally (`src/activity_counts.h`)

The screen shows `ALERTS: n` (red) and `CAUTIONS: n` (amber) — a count of
**episodes**, decaying over time. It replaced the old `Total events` line, which
counted every detection-engine fire since boot and only ever grew; customers
read "530" as 530 alerts and concluded the opposite of the truth. The lifetime
figure still exists, but moved to the **serial status line as `events=`** where
it is a diagnostic rather than something a user can misread.

- Counted on the **same rising edge that fires the chime** (`ui_task.h`), so the
  number on screen can never disagree with what was heard. Only a *rise* counts:
  a device parked at ALERT for an hour is one alert, not one per tick.
- Reaching ALERT directly from CLEAR counts one alert and **no** caution — the
  caution level was never reached.
- Decay is one point per `ACTIVITY_DECAY_MS` (120 s, deliberately the same span
  as `DETECTION_RESCORE_MS` so the tally and the reading fade together), and it
  is **independent of new events**, so a quiet spell returns it to zero.
- Capped at `ACTIVITY_COUNT_MAX` (99): keeps the on-screen width constant so the
  layout cannot shift mid-alert.
- **Do not show `events=` (the lifetime count) on the display strip.** The strip
  truncates at `MBE_LOG_LINE_LEN` (53 chars) and the full status line with
  `events=` measured 55 — it would be silently clipped, with nothing to report
  it. It is serial-only for that reason.

### Source MAC on screen

The address of the device that triggered a detection is shown directly beneath
the detection type, in the solid severity colour (`mbe_lastMac` / `msce_lastMac`
/ the `mac` parameter of `c5DisplayScore()`). It is threaded through
`UiSnapshot.lastMac` under the same critical section as `lastDet` **so the two
can never disagree on screen** — a stale address under a fresh label would
misidentify an innocent device.

- It is captured by `esNotePendingMac6()` / `esNotePendingMacStr()` at the
  moment an engine matches, and latched into `g_lastDetMac` by `mbeDetTrack()`
  alongside the label. **Latch both or neither.**
- A trigger with no address (an SSID-keyword match — the broadcaster's
  identifier *is* the SSID) renders as `MAC --`, never as the previous
  detection's address.
- **Boards differ in width, so the drawing differs but the content does not:**
  Core2/Basic and StickC are ≥240 px wide and print the full 17-character
  address on one line; the T-Dongle C5 is 80 px wide and splits it at the
  colon boundary (`aa:bb:cc` / `dd:ee:ff`).

### Critical-alert screen hold (`src/alert_hold.h`)

A `SCORE_ALERT` detection pins the panel for `EA_HOLD_MS` (15 s) so the severity
and the source address stay readable after the score has already decayed —
otherwise both are replaced before they can be read or written down.

- The **decision logic is shared** in `alert_hold.h`; each display header only
  draws. Do not copy the state machine into a fourth display header — three
  copies of a timer is exactly the drift this repo has been bitten by.
- **Nothing on the held panel blinks**, and the countdown is refreshed once per
  whole second (`ALERT_HOLD_TICK`) rather than redrawing the panel every tick —
  a repaint several times a second makes the address unreadable.
- A held alert re-arms on a **changed source** (address *or* detection label),
  so a second critical device gets its own full window.
- **Cautions never hold.** They are common; pinning the screen for each would
  make the device unusable.
- A source that is *still* critical when a hold expires does **not** re-hold:
  the live panel already shows the same severity and address, and re-holding
  every tick would freeze the screen indefinitely.

## Test tooling that exercises these paths

- `test/test_oui_matching/`, `test/test_ssid_ble_matching/`,
  `test/test_tracker_follow/` and `test/test_alert_hold/` (native/host Unity
  tests, **82 test cases total**) — exercise `es_detect.h`'s pure matching
  helpers (`ouiMatch()`, `ssidHas()`, `strContainsCI()`, `fwDefaultMacMatch()`,
  `ravenServiceInRange()`, `trackerFollowUpdate()`), pattern-table integrity
  (counts, mutual exclusivity, null-termination) and `alert_hold.h`'s hold
  state machine. All run directly on the host via `pio test -e native` or the
  platformio-mcp `run_tests` tool — no hardware required.
  **The tracker-follow and alert-hold suites are only possible because both
  state machines take `now` as a parameter**: that is what lets a 30-minute
  follow window and a 15-second screen hold be driven through their entire
  lifecycle in microseconds, deterministically. Keep that property when
  editing either one; calling `millis()` inside the state machine would make
  the interesting edges (threshold crossing, expiry, re-arm, wrap) untestable
  and would force those tests back onto hardware.
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
