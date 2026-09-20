# Eye Spy

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)
[![Author](https://img.shields.io/badge/Author-SimeonOnSecurity-green.svg)](https://github.com/simeononsecurity)


> Passive surveillance detector for the **M5Stack Atom Lite** (ESP32-PICO-D4).  
> Scans BLE and WiFi for body cameras, ALPR systems, AirTags, trackers, drones, and hidden cameras — LED turns red when something is watching.

Continuously monitors Bluetooth LE and WiFi for recording devices, surveillance cameras, ALPR (license-plate reader) systems, drones, and personal trackers. A confidence-score system drives a single RGB LED indicator so you get an at-a-glance privacy threat level at all times.

**Printable guides for non-technical users:**
[4x6 pocket card](docs/print/quick-start-4x6.pdf) &middot;
[8.5x11 sheet](docs/print/quick-start-letter.pdf)

---

## LED Indicators

| Color | Meaning | Score |
|---|---|---|
| 🔵 Blue pulse | Startup / first scan | — |
| 🟢 Green solid | Clear — nothing detected | 0–2 |
| 🟡 Yellow solid | Caution — possible recording device nearby | 3–5 |
| 🔴 Red flashing | Alert — definite surveillance / tracking device detected | 6+ |

Score decays −1 point every 60 seconds. Each detection type has a 120-second re-score cooldown so a persistent device can't infinitely stack points.

---

## Detection Engines

### BLE — passive (NimBLE, no scan requests transmitted)

| # | Target | What's Detected | Score |
|---|---|---|---|
| 1 | **Axon body camera** | BLE MAC OUI `00:25:df` (Axon — body cams, tasers, LE equipment) | +5 🔴 |
| 2 | **Ray-Ban Meta** smart glasses | BLE service UUID `0xFD5F` | +5 🔴 |
| 3 | **Flock Safety BLE** | BLE device name containing "Flock", "Raven", "Penguin", "Pigvision", "FS Ext Battery" or "DfuTarg", **or** matching a firmware-derived name *shape*: `Penguin-` + 10 digits, a bare 10-digit serial, `FS Ext Battery`, `DfuTarg` | +5 🔴 |
| 4 | **Flock accessory GATT** (battery pack) | Advertised service UUID = Flock accessory service `e8ccbb38-9532-46a8-9fe5-1814df172e6f` or Nordic legacy DFU service `00001530-1212-efde-1523-785feabcd123` | +5 🔴 |
| 5 | **Raven surveillance device** | Advertised service UUID matches the named Raven GATT services **or falls anywhere in the Raven 16-bit range `0x3100`–`0x3500`** — the range catches `0x3101`/`0x3102`, which expose GPS unauthenticated | +5 🔴 |
| 6 | **Card skimmer** (HC-03/05/06) | BLE device name exact match — Bluetooth modules commonly found in payment-terminal skimmers | +5 🔴 |
| 7 | **Apple AirTag** | Manufacturer data `0x004C` subtype `0x12`/`0x1E`, or raw payload `1E FF 4C 00` / `4C 00 12` | +4 🔴 |
| 8 | **Drone (OpenDroneID BLE)** | BLE service UUID `0xFFFA`, or raw AD service-data payload with app code `0x0D` | +4 🔴 |
| 9 | **Samsung SmartTag** | BLE service UUID `0xFD5A` | +3 🟡 |
| 10 | **Tile tracker** | BLE service UUID `0xFEED` or `0xFEEC` | +3 🟡 |
| 11 | **MeshCore node** | BLE device name prefix `MeshCore-` | +2 🟡 |
| 12 | **iBeacon** (retail/venue tracking) | Manufacturer data `0x004C 0x02 0x15` — deployed in stores, airports, stadiums to track movement | +2 🟡 |
| 13 | **Unknown persistent device** | Any unclassified BLE MAC seen ≥3× over ≥5 minutes (device scout / follower detection) | +2 🟡 |

### WiFi scan — active channel scan

| # | Target | What's Detected | Score |
|---|---|---|---|
| 14 | **Flock Safety camera** (OUI) | BSSID matches the 35-entry Flock Safety OUI table (`b4:1e:52`, `82:6b:f2`, `70:c9:4e`, FS-Ext-Battery prefixes, …) | +5 🔴 |
| 15 | **Unprovisioned Flock camera** (firmware-default MAC) | BSSID is **exactly** `00:03:7f:50:00:01` or `00:03:7f:4f:00:16` — the QCA9377 factory-default radio addresses from the camera firmware image. Full 6-byte match, and the only WiFi signal that reaches the alert threshold on its own (see below) | +6 🔴 |
| 16 | **ALPR / LPR camera** (OUI) | BSSID matches Motorola Solutions / Vigilant Solutions OUI `00:0e:58` | +5 🔴 |
| 17 | **Flock keyword SSID** | SSID contains: `flock`, `flocksafety`, `fs ext`, `penguin`, `pigvision`, `raven` | +5 🔴 |
| 18 | **ALPR keyword SSID** | SSID contains: `alpr`, `lpr`, `vigilant`, `plateread`, `licenseplat`, `motorola`, `automate` | +4 🔴 |
| 19 | **Surveillance camera vendor** (OUI) | BSSID matches 31-entry camera OUI table — Hikvision, Dahua, Axis, Ring, Nest, Arlo, Wyze, Reolink, FLIR, Amcrest, Vivotek, Hanwha, Mobotix, Ubiquiti UniFi | +3 🟡 |
| 20 | **Camera keyword SSID** | SSID contains: `cam`, `ipcam`, `cctv`, `nvr`, `dvr`, `doorbell`, `surv`, `blink`, `lorex`, `protect`, `genetec`, and more | +2 🟡 |
| 21 | **Flock contract-mfr OUI** | BSSID matches the 7-entry contract-manufacturer table (Liteon/USI, plus `00:03:7f` Qualcomm Atheros — the camera's QCA9377 radio block, shared with unrelated Atheros gear) | +2 🟡 |

### WiFi promiscuous — passive sniff, channel-hopping

| # | Target | What's Detected | Score |
|---|---|---|---|
| 22 | **Drone (OpenDroneID WiFi NaN)** | 802.11 Management frame to destination `51:6f:9a:01:00:00` — ASTM F3411 Remote ID broadcast | +4 🔴 |

---

## How Scoring Works

- **Score 0–2 → 🟢 GREEN** — no significant detections
- **Score 3–5 → 🟡 YELLOW** — possible device nearby, worth being aware
- **Score 6+ → 🔴 RED** — definite surveillance / tracking equipment detected

A single hit from a definite device (Axon, Flock, ALPR OUI, AirTag) immediately reaches the alert threshold in one detection. Multiple independent caution-level signals combine to raise the alert level. The one deliberate exception is the firmware-default MAC engine (+6): a full 6-byte match on a factory-default radio address only exists on a never-provisioned camera, so it is allowed to alert alone — no other WiFi engine is.

**Decay:** score drops −1 per 60 s of inactivity. If you leave the area, the device returns to clear within a few minutes.

**Re-score cooldown:** the same detection type won't re-add points for 120 seconds, preventing runaway scoring from one persistent device.

---

## Phase Schedule

```
BLE passive (9 s) → WiFi scan (~3 s) → Promiscuous sniff (5 s) → repeat
```

BLE is stopped before any WiFi operations to respect the shared ESP32 radio. It restarts cleanly at the beginning of each cycle.

During the promiscuous phase, the radio channel-hops across `{1, 6, 11, 3, 8, 13}` every 400 ms to maximize drone NaN frame coverage.

---

## Hardware

### M5Stack Atom Lite (primary)

| | |
|---|---|
| Board | M5Stack Atom Lite |
| MCU | ESP32-PICO-D4 |
| LED | SK6812 NeoPixel on GPIO27 |
| Button | GPIO39 (input only) |
| Flash | 4 MB |

### LILYGO T-Dongle C5 (experimental)

| | |
|---|---|
| Board | LILYGO T-Dongle C5 |
| MCU | ESP32-C5 RISC-V, dual-band WiFi 6 + BT 5 |
| Display | ST7735S 80×160 colour TFT |
| RGB LED | WS2812B on GPIO11 |
| BOOT button | GPIO9 |
| Flash | 4 MB |

**T-Dongle C5 TFT display layout:**

| Score | Background | Large score # | Status |
|---|---|---|---|
| 0–2 | Black | 🟢 Green | CLEAR |
| 3–5 | Dark amber | 🟡 Yellow | CAUTION |
| 6+ | Dark red | 🔴 Red | ALERT |

Current scan phase (BLE / WIFI / PROMISC) is shown at the bottom of the screen.
The WS2812B RGB LED mirrors the score level (green / amber / red) for at-a-glance
visibility without looking at the screen.

---

## Build & Flash

```bash
# Compile for Atom Lite
pio run -e atom-lite

# Flash
pio run -e atom-lite -t upload

# Serial monitor (115200 baud)
pio device monitor -b 115200

# Generic ESP32 devkit (LED on GPIO2)
pio run -e esp32dev

# LILYGO T-Dongle C5 (experimental — ESP32-C5 dual-band WiFi 6 + BT 5)
pio run -e lilygo-t-dongle-c5 -t upload
```

Dependencies (installed automatically by PlatformIO):
- `adafruit/Adafruit NeoPixel @ ^1.15.1`
- `h2zero/NimBLE-Arduino @ ^1.4.3`
- `adafruit/Adafruit ST7735 and ST7789 Library @ ^1.10.4` *(T-Dongle C5 only)*
- `adafruit/Adafruit GFX Library @ ^1.11.9` *(T-Dongle C5 only)*

---

## Serial Output

All output is prefixed with `[eyespy]`:

```
[eyespy] Eye Spy v1.1 starting
[eyespy] init OK
[eyespy] BLE scan start
[eyespy] WiFi scan
[eyespy] Flock-cam OUI d4:bb:e6  "Flock_CAM_0032"
[eyespy] +5 (Flock-cam-OUI)  score=5
[eyespy] WiFi done  score=5
[eyespy] promisc ON
[eyespy] status  score=5  CAUTION  phase=PROMISC  tracked=12
[eyespy] Axon-cam  RSSI=-62
[eyespy] +5 (Axon-cam)  score=10
[eyespy] status  score=10  ALERT  phase=BLE  tracked=12
[eyespy] decay  score=9
```

---

## Flock Safety OUI Table

The 35-entry `FLOCK_OUIS` table (`src/es_detect.h`) covers:
- `d4:bb:e6`, `3c:61:05` — Flock Safety (IEEE registered, legacy entries)
- `b4:1e:52` — Flock Safety direct IEEE assignment
- `82:6b:f2` — DeFlockJoplin field-verified camera (locally-administered MAC)
- 6 FS Ext Battery device-series prefixes
- 25 additional MAC prefixes from the @NitekryDPaul promiscuous-mode dataset

These are checked **separately** from the general ALPR table, so both Flock cameras and Vigilant LPR cameras can score independently in the same scan cycle. A separate low-confidence `FLOCK_MFR_OUIS` table (Liteon/USI contract-manufacturer prefixes shared with non-Flock hardware) scores +2 instead of +5.

---

## 🧪 Native Unit Tests

The detection pattern library (`src/es_detect.h`) is fully tested via a
host-side Unity test suite — no ESP32 hardware needed. This mirrors the
equivalent test setup in the sibling
[flock-you-esp32](https://github.com/simeononsecurity/flock-you-esp32)
project:

```bash
cd eye-spy
pio test -e native                              # run all 46 tests
pio test -e native -f test_oui_matching          # OUI table + firmware-default MAC matching (19)
pio test -e native -f test_ssid_ble_matching     # SSID / BLE-name / GATT / Raven-range matching (27)
```

All **46 tests pass** against the current `es_detect.h`. The test suite covers:
- All 35 `FLOCK_OUIS`, 7 `FLOCK_MFR_OUIS` (incl. `00:03:7f`), and 31 `CAM_OUIS` prefixes
- SoundThinking and ALPR OUI isolation (not present in any other table)
- Cross-table mutual-exclusion (no OUI prefix appears in more than one table)
- `FLOCK_SSID_KW` / `ALPR_SSID_KW` / `CAM_SSID_KW` keyword matching (case-insensitive)
- `FLOCK_BLE_NAMES` substring matching via `strContainsCI()`
- **Firmware-default MACs must match all six bytes** — near-misses inside the
  same `00:03:7f` block (`…:50:00:02`) must not, and a random `00:03:7f` device
  must be mfr-tier only, never the strong signal
- BLE name **shapes**: bare 10-digit serial, `Penguin-` + 10 digits,
  `FS Ext Battery`, `DfuTarg`, plus rejection of wrong digit counts / trailing junk
- `FLOCK_GATT_UUIDS` contents, and that the Flock accessory service is *not*
  presented as a Raven UUID
- Raven service **range** `0x3100`–`0x3500` (including `0x3101`/`0x3102`, the
  GPS-leaking services the named table alone missed) and out-of-range rejection
- 16-bit service parsing from both UUID shapes (canonical 128-bit and `0x3101`)
- `SKIMMER_NAMES` and `RAVEN_UUIDS` table contents/counts
- nullptr-termination sanity for every pattern array

---

## Notes


- BLE scanning is **passive** — no scan requests are transmitted. The device is not detectable by the equipment it is scanning for.
- RSSI threshold: devices weaker than −90 dBm are ignored to reduce false positives in dense environments.
- The device persistence tracker watches up to 50 unknown BLE MACs simultaneously. Entries are purged after 30 minutes of absence.
- The iBeacon detector fires on standard Apple iBeacon format (`0x4C 0x00 0x02 0x15`) regardless of UUID, targeting retail-tracking deployments.
