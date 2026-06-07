# Eye Spy v1.0

Passive surveillance-device detector for the **M5Stack Atom Lite** (ESP32-PICO-D4).

Continuously scans Bluetooth LE and WiFi for recording devices, security cameras, ALPR (license-plate reader) systems, and drones. Uses a confidence-score system to drive a single RGB LED indicator.

---

## LED Indicators

| Color | State | Score |
|---|---|---|
| 🔵 Blue pulse | Startup / first scan cycle | — |
| 🟢 Green solid | Clear — no surveillance detected | 0–2 |
| 🟡 Yellow solid | Caution — possible recording device nearby | 3–5 |
| 🔴 Red flashing | Alert — definite recording / ALPR device detected | 6+ |

Score decays −1 point every 60 seconds when detections stop.

---

## Detection Engines

### BLE (passive — no scan requests transmitted)

| # | Target | Signal | Score |
|---|---|---|---|
| 1 | **Axon body camera** | BLE MAC OUI `00:25:df` | +5 |
| 2 | **Ray-Ban Meta** smart glasses | BLE service UUID `0xFD5F` | +5 |
| 3 | **Drone (OpenDroneID)** | BLE service UUID `0xFFFA` or raw AD payload | +4 |

### WiFi scan (active channel scan)

| # | Target | Signal | Score |
|---|---|---|---|
| 4 | **ALPR / LPR camera** | BSSID OUI — Motorola/Vigilant, Flock Safety | +5 |
| 5 | **ALPR keyword SSID** | "alpr", "lpr", "flock", "vigilant", "plateread"… | +4 |
| 6 | **Surveillance camera vendor** | BSSID OUI — Hikvision, Dahua, Axis, Ring, Nest, Arlo, Wyze, Reolink, FLIR, Amcrest, Ubiquiti… | +3 |
| 7 | **Camera keyword SSID** | "cam", "ipcam", "cctv", "dvr", "nvr", "doorbell", "surv", "blink"… | +2 |

### WiFi promiscuous (passive sniff, channel-hopping)

| # | Target | Signal | Score |
|---|---|---|---|
| 8 | **Drone (OpenDroneID NaN)** | 802.11 Management frame to `51:6f:9a:01:00:00` | +4 |

---

## Phase Schedule

```
BLE passive (9 s) → WiFi scan (~3 s) → Promiscuous sniff (5 s) → repeat
```

BLE is stopped before WiFi operations to respect the shared radio; it restarts cleanly at the top of each cycle.

---

## Hardware

| Item | Value |
|---|---|
| Board | M5Stack Atom Lite |
| MCU | ESP32-PICO-D4 |
| LED | SK6812 on GPIO 27 |
| Button | GPIO 39 |
| Flash | 4 MB |

---

## Build

```bash
# Atom Lite target
pio run -e atom-lite

# Generic ESP32-DevKit target (LED on GPIO2)
pio run -e esp32dev

# Flash
pio run -e atom-lite -t upload
```

Requires PlatformIO with:
- `adafruit/Adafruit NeoPixel @ ^1.15.1`
- `h2zero/NimBLE-Arduino @ ^1.4.3`

---

## Serial Monitor

```
[eyespy] Eye Spy v1.0 starting
[eyespy] init complete
[eyespy] BLE scan started
[eyespy] WiFi scan start
[eyespy] camOUI    bc:ad:28  SSID="HikVision_Cam_01"  RSSI=-68
[eyespy] +3 (camOUI)  score=3
[eyespy] WiFi scan done  score=3
[eyespy] promiscuous ON
[eyespy] status  score=3  state=CAUTION  phase=PROMISC
```

Baud rate: **115200**. All output prefixed with `[eyespy]`.

---

## Score Behavior

- Each detection type has a **120-second re-score cooldown** — the same device won't stack points faster than every 2 minutes.
- Score decays **−1 point per 60 seconds**.
- Any single definite detection (Axon, Ray-Ban, ALPR OUI) immediately pushes score to ALERT level (6+) in one hit.

---

## Limitations / Notes

- BLE and WiFi time-share the ESP32 radio. BLE scanning is paused during WiFi operations.
- Promiscuous WiFi sniff is channel-hopped across {1, 6, 11, 3, 8, 13} at 400 ms intervals.
- RSSI threshold (default −90 dBm) filters out weak / distant devices to reduce false positives in dense environments.
- Camera vendor OUI matching covers the AP (router/hotspot) radios of IP cameras, not the camera sensor modules.
