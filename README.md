# Eye Spy v1.1

> Passive surveillance detector for the **M5Stack Atom Lite** (ESP32-PICO-D4).  
> Scans BLE and WiFi for body cameras, ALPR systems, AirTags, trackers, drones, and hidden cameras — LED turns red when something is watching.

Continuously monitors Bluetooth LE and WiFi for recording devices, surveillance cameras, ALPR (license-plate reader) systems, drones, and personal trackers. A confidence-score system drives a single RGB LED indicator so you get an at-a-glance privacy threat level at all times.

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
| 3 | **Flock Safety BLE** | BLE device name containing "Flock", "Penguin", "Pigvision", or "FS Ext Battery" | +5 🔴 |
| 4 | **Card skimmer** (HC-03/05/06) | BLE device name exact match — Bluetooth modules commonly found in payment-terminal skimmers | +5 🔴 |
| 5 | **Apple AirTag** | Manufacturer data `0x004C` subtype `0x12`/`0x1E`, or raw payload `1E FF 4C 00` / `4C 00 12` | +4 🔴 |
| 6 | **Drone (OpenDroneID BLE)** | BLE service UUID `0xFFFA`, or raw AD service-data payload with app code `0x0D` | +4 🔴 |
| 7 | **Samsung SmartTag** | BLE service UUID `0xFD5A` | +3 🟡 |
| 8 | **Tile tracker** | BLE service UUID `0xFEED` or `0xFEEC` | +3 🟡 |
| 9 | **MeshCore node** | BLE device name prefix `MeshCore-` | +2 🟡 |
| 10 | **iBeacon** (retail/venue tracking) | Manufacturer data `0x004C 0x02 0x15` — deployed in stores, airports, stadiums to track movement | +2 🟡 |
| 11 | **Unknown persistent device** | Any unclassified BLE MAC seen ≥3× over ≥5 minutes (device scout / follower detection) | +2 🟡 |

### WiFi scan — active channel scan

| # | Target | What's Detected | Score |
|---|---|---|---|
| 12 | **Flock Safety camera** (OUI) | BSSID matches 22-entry Flock Safety OUI table (`d4:bb:e6`, `3c:61:05`, FS-Ext-Battery prefixes) | +5 🔴 |
| 13 | **ALPR / LPR camera** (OUI) | BSSID matches Motorola Solutions / Vigilant Solutions OUI `00:0e:58` | +5 🔴 |
| 14 | **Flock keyword SSID** | SSID contains: `flock`, `flocksafety`, `fs ext`, `penguin`, `pigvision` | +5 🔴 |
| 15 | **ALPR keyword SSID** | SSID contains: `alpr`, `lpr`, `vigilant`, `plateread`, `licenseplat`, `motorola`, `automate` | +4 🔴 |
| 16 | **Surveillance camera vendor** (OUI) | BSSID matches 31-entry camera OUI table — Hikvision, Dahua, Axis, Ring, Nest, Arlo, Wyze, Reolink, FLIR, Amcrest, Vivotek, Hanwha, Mobotix, Ubiquiti UniFi | +3 🟡 |
| 17 | **Camera keyword SSID** | SSID contains: `cam`, `ipcam`, `cctv`, `nvr`, `dvr`, `doorbell`, `surv`, `blink`, `lorex`, `protect`, `genetec`, and more | +2 🟡 |

### WiFi promiscuous — passive sniff, channel-hopping

| # | Target | What's Detected | Score |
|---|---|---|---|
| 18 | **Drone (OpenDroneID WiFi NaN)** | 802.11 Management frame to destination `51:6f:9a:01:00:00` — ASTM F3411 Remote ID broadcast | +4 🔴 |

---

## How Scoring Works

- **Score 0–2 → 🟢 GREEN** — no significant detections
- **Score 3–5 → 🟡 YELLOW** — possible device nearby, worth being aware
- **Score 6+ → 🔴 RED** — definite surveillance / tracking equipment detected

A single hit from a definite device (Axon, Flock, ALPR OUI, AirTag) immediately reaches the alert threshold in one detection. Multiple independent caution-level signals combine to raise the alert level.

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

| | |
|---|---|
| Board | M5Stack Atom Lite |
| MCU | ESP32-PICO-D4 |
| LED | SK6812 NeoPixel on GPIO27 |
| Button | GPIO39 (input only) |
| Flash | 4 MB |

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
```

Dependencies (installed automatically by PlatformIO):
- `adafruit/Adafruit NeoPixel @ ^1.15.1`
- `h2zero/NimBLE-Arduino @ ^1.4.3`

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

The 22-entry `FLOCK_OUIS` table covers:
- `d4:bb:e6` — Flock Safety (IEEE registered)
- `3c:61:05` — Flock Safety (IEEE registered)
- 20 additional MAC prefixes observed on Flock FS-Ext-Battery and Flock Wi-Fi camera hardware

These are checked **separately** from the general ALPR table, so both Flock cameras and Vigilant LPR cameras can score independently in the same scan cycle.

---

## Notes

- BLE scanning is **passive** — no scan requests are transmitted. The device is not detectable by the equipment it is scanning for.
- RSSI threshold: devices weaker than −90 dBm are ignored to reduce false positives in dense environments.
- The device persistence tracker watches up to 50 unknown BLE MACs simultaneously. Entries are purged after 30 minutes of absence.
- The iBeacon detector fires on standard Apple iBeacon format (`0x4C 0x00 0x02 0x15`) regardless of UUID, targeting retail-tracking deployments.
