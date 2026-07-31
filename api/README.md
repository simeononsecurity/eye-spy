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
[eyespy] decay  score=9
```
