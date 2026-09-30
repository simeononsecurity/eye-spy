# Changelog

All notable user-visible changes to Eye Spy are recorded here. The format
follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

Rationale for a root-cause note on most entries: this project has repeatedly
fixed the same class of bug more than once — detections silently dropped by the
dashboard's text parser, and scoring changes that made a deliberately-quiet
signal cross the alert threshold and hold the status LED red. A future reader
(and the next person answering a customer) should be able to tell *why* an
entry exists without re-deriving the investigation; see `.clinerules/` for the
durable version of those lessons and `docs/customer-replies.md` for what
customers were told.

## [Unreleased]

### Added

- **Audible alerts on the boards that have a speaker.** The M5Stack Basic and
  Core2 For AWS builds had `USE_M5_SPEAKER 0`, so `audioAlert()` compiled down to
  a no-op — no sound was possible no matter how close a threat was. Three
  customers reported "the alert is only the vibration" because of it. Alert is
  now a **rising two-tone chime (G6 1568 Hz → C7 2093 Hz)** and caution a
  **single lower tone (A5 880 Hz)**, deliberately mirroring the vibration's
  2-pulse/1-pulse distinction. The chime is non-blocking (armed by
  `audioAlert()`, stepped by `audioAlertTick()` from the UI task) so it can never
  stall a redraw.
- **The source MAC address on screen**, directly beneath the detection type and
  in the solid severity colour, so a user can identify the device that triggered
  an alert and write it down. Carried in the same snapshot field as the
  detection label so the two can never disagree. `MAC --` means the trigger
  carried no address (an SSID-only match) and deliberately does not fall back to
  a previous detection's address.
- **A 15-second hold on critical alerts.** A critical detection is usually
  momentary, so the live dashboard replaced both the severity and the address
  before either could be read. The panel now pins the severity, detection type
  and source MAC with a countdown, and nothing on it blinks.
- **Decaying alert/caution tallies** on screen (`ALERTS: n` red, `CAUTIONS: n`
  amber) counting *episodes* and fading one point every two minutes.
- **AirTag / SmartTag / Tile following gate** — these no longer score on a
  sighting. They must be continuously in range for ~30 minutes before
  contributing anything, so a passer-by's tracker cannot push the device toward
  an alert. Keyed on the tracker *class* rather than the MAC address, because
  AirTags rotate their address roughly every 15 minutes while separated from
  their owner — a window keyed on the address could never be satisfied.
- **Firmware-derived Flock signatures**: exact match on the factory-default
  QCA9377 radio MACs, the Flock accessory / Nordic DFU GATT services, the Raven
  service range (catching the GPS-leaking `0x3101`/`0x3102`), BLE name shapes
  (bare 10-digit serial, `Penguin-`+10, `FS Ext Battery`, `DfuTarg`), and
  `14:b5:cd` + the CVE-2025-59409 `flck` SSID spelling.
- **Printable guides** (4×6 card + 8.5×11 sheet) and
  [`docs/customer-replies.md`](docs/customer-replies.md) — ready-made answers to
  the questions that actually come back from the field.
- **Web-flasher configurator** (ADR-0001): before flashing, choose which
  detections this device watches for (13 user-facing groups covering all 24
  engines), which of its own outputs it uses (light, sound, vibration — only the
  ones the selected board actually has), and three bounded sensitivity settings
  (how sure before it alerts, Bluetooth proximity floor, and how long a tracker
  must follow you). Everything defaults to the current behaviour, so leaving it
  alone changes nothing.
- Native host test suite (113 cases) covering the detection tables, the tracker
  gate, the alert hold and the activity tallies.

### Changed

- **Raven UUID range matches no longer alert.** Only the named services do. A
  device with a **randomised MAC at −94 dBm** whose service merely fell inside
  the unassigned `0x3100`–`0x3500` block was scored as a standalone camera at 45,
  chirped, and held the LED red — and because randomised addresses rotate, the
  per-MAC dedupe could never suppress it. That is what "stuck red" was. In-range
  matches are still recorded, logged and exported, at a level below the alert
  threshold.
- **Standard Bluetooth SIG services no longer alert.** `0x180A` (Device
  Information), `0x1809` (Health Thermometer) and `0x1819` (Location) are on
  essentially every BLE device ever made, so at alert-level scoring an ordinary
  fitness band alerted as a "Raven camera". They are now firmware-estimation
  evidence only, and a matcher guard prevents re-adding one to the alert table.
- `Total events` — a counter that only ever grew and was widely misread as an
  alert count — replaced by the decaying tallies above.
- The lifetime event count moved to the serial status line as `events=`.

### Fixed

- **Crash evidence is no longer discarded.** A field unit reported rebooting on
  its own and appearing to lose its session. That could not be acted on, because
  the things "reboot" can mean — a firmware panic, an interrupt/task watchdog, a
  brownout, a deliberate software reset — each have a different remedy, and this
  firmware recorded none of them.
  - The firmware now prints **why it last restarted** and its **heap low-water
    mark** at every boot, before display/radio init so the reason survives a hang
    during init:
    ```
    [eyespy] boot: reset=POWERON (cold boot / power cycle)
    [eyespy] boot: heap=214880 min_heap=201336
    ```
    Any reason other than a cold boot is suffixed `<-- investigate`. A heap floor
    that keeps falling across a run is the signature of a leak, which is the usual
    cause of a reboot that appears to arrive on a timer.
  - **`heap=`/`min_heap=` are also on the periodic status line**, not just at boot,
    because the boot value cannot distinguish "always was this low" from "falling
    steadily". `min_heap` is the low-water mark and so only ever falls, which is
    what makes a slow leak visible in a log someone is already capturing while a
    unit runs unattended:
    ```
    [eyespy] status  score=10  ALERT  phase=BLE  tracked=3  events=12  heap=214880 min_heap=201336
    ```
    Appending fields here is safe, and this one needed checking: the dashboard's
    `_RE_STATUS` regex *does* parse this line. It is unanchored, so it still
    matches and still captures the same four fields. Verified by running the real
    `_RE_*` patterns from `api/eyespy.py` against the extended line.
  - A **`coredump` partition** was added to `partitions_4mb.csv` (48 KB), so a
    panic is written to flash and can be retrieved later with `esptool` — which
    matters for a unit that ran unattended. Eye Spy's partition table already
    filled the whole 4 MB, so the space comes from **shrinking the `spiffs`
    partition** (`0xF000 → 0x3000`). That is lossless here because this firmware
    never mounts SPIFFS — the partition is vestigial (verified: no
    SPIFFS/LittleFS call exists in `src/`). `escfg` deliberately keeps its offset
    (`0x3FF000`) so the web flasher's config part URL does not move.
  - Root cause of the missing coredump, because the previous reasoning was wrong:
    `sdkconfig.defaults` claimed to have disabled coredump-to-flash in order to
    silence the boot-time "No core dump partition found!" warning. **That setting
    never applied.** The Arduino core ships *precompiled* ESP-IDF libraries whose
    baked-in Kconfig already has `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y` (verified
    in the packaged `tools/sdk/esp32/sdkconfig` and `.../esp32s3/sdkconfig`), and
    no project `sdkconfig.defaults` can alter a prebuilt `.a`. The warning was
    telling the truth: the backend was compiled in, and only the **partition** was
    missing. Fixed by adding the partition rather than by changing a setting, and
    `sdkconfig.defaults` now documents this instead of asserting the opposite.

- **Every Raven BLE detection was silently dropped by the dashboard API.** The
  firmware printed a single space before `RSSI=` where the API's pattern
  required two, so the line parsed, failed to match, and returned nothing — it
  never reached the dashboard, the session or any export, with no error
  anywhere. The pattern is now tolerant and the emitter uniform. This was the
  third instance of the same failure mode (see also the SSID lines and the
  padded right-aligned OUI lines).
- **SSID-only detections never reached the dashboard** — the parser's WiFi
  patterns required the literal word "OUI", so `[eyespy] Flock SSID "…"` matched
  nothing. Found by instantiating every firmware log line and running it through
  the real parser.
- Silently-swallowed error returns: `esp_wifi_80211_tx()` /
  `esp_wifi_set_channel()` in the beacon tester, `WiFi.softAP()`, and
  `M5.Speaker.begin()` — the last of which reports success even when it fails,
  making total silence indistinguishable from working audio.
- Display flicker (the stale-tick redraw), and a counter that could not decay
  across the `millis()` wrap.

### Removed

- The claim that Eye Spy is silent. The printable guide stated "Eye Spy is
  silent — no buzzer", which was wrong for every audio-capable board and is
  exactly the impression the three customers formed. It now describes the beeps.
