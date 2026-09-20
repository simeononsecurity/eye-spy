# Eye Spy — Dashboard API

Local Flask + SocketIO server that reads the Eye Spy firmware over serial and provides a real-time dashboard with GPS wardriving support.

## Quick start

```bash
cd eye-spy/api
pip install -r requirements.txt
python eyespy.py
```

Open **http://localhost:5001** in your browser.

## Features

- Real-time score gauge driven by live serial output
- GPS wardriving — connect a USB NMEA GPS puck *or* use `gpsd`
- Leaflet map with colour-coded markers (green/amber/red per threat level)
- CSV / JSON / KML export
- Persistent session storage (survives server restarts)

## GPS options

| Method | Detail |
|---|---|
| USB NMEA dongle | Select the GPS serial port in the dashboard |
| gpsd | Set `GPS_PORT=gpsd` in environment — connects to `localhost:2947` |
| Browser geolocation | Use the static GitHub Pages dashboard instead |

## Serial output this app understands

```
[eyespy] +5 (Axon-cam)  score=10
[eyespy] status  score=10  ALERT  phase=BLE  tracked=12
[eyespy] Axon-cam  RSSI=-62
[eyespy] Flock-cam OUI d4:bb:e6  "Flock_CAM_0032"
[eyespy] Flock-FW-default MAC 00:03:7f:50:00:01 "Flock-4f00c1"
[eyespy] Flock SSID "FS Ext Battery"
[eyespy] decay  score=9
```

> Every detection-producing line above parses (`_RE_WIFI`, `_RE_FWMAC`,
> `_RE_SSID`, `_RE_BLE`). If you add a new `Serial.printf("[eyespy] …")`
> detection line to the firmware, check it against this parser — unrecognised
> lines are dropped silently. See `.clinerules/02-test-before-commit.md` item
> 10 for the verification method.

## Firmware-signature tagging

Detections matching signatures extracted from a real Flock camera firmware
image (see `datasets/firmware_derived_signatures.md`) get two extra fields:

- **`matched_signatures`** — ordered list of tags, e.g. `oui:b4:1e:52`,
  `mac:fw_default`, `ssid_keyword:penguin`, `gatt:flock_accessory`,
  `gatt:raven_service`, `ble_mfg:0x09c8`, `classic_bt_name:msm8953_32`.
- **`firmware_sig`** — `True` when at least one matched.

Both appear in the CSV export. Tags are derived from the parsed `oui` field and
the `detection_method` string (plus `ssid`), so they work for live serial lines
and imports alike. Two caveats inherent to this API's text-line parsing:

- The exact factory-default MAC is tagged from the `Flock-FW-default MAC`
  method rather than by comparing a MAC — the API only sees the OUI prefix.
- Name-based tags (`ble_name:penguin_serial`, `bare_serial`, `dfutarg`) need a
  `device_name` field, which the firmware's BLE log line does not carry — so
  those tags only appear on imported records.

Community field-research OUIs (@NitekryDPaul / DeFlockJoplin) are deliberately
**not** tagged; this tag set means "evidence found in the camera firmware
image".
