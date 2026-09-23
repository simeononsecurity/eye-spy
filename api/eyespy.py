#!/usr/bin/env python3
"""eyespy.py — Flask + SocketIO dashboard for Eye Spy firmware (port 5001)"""
from flask import Flask, render_template, jsonify, request, send_file
from flask_socketio import SocketIO, emit, join_room
import json, csv, os, re, time, threading, pickle, io
from datetime import datetime
from pathlib import Path
import serial, serial.tools.list_ports

app = Flask(__name__)
app.config['SECRET_KEY'] = os.environ.get('SECRET_KEY', 'eyespy_dev_key_2025')
socketio = SocketIO(app, cors_allowed_origins='*', async_mode='threading')

detections = []; score_history = []
g_score = 0; g_status = 'CLEAR'; g_phase = '?'; g_tracked = 0
gps_data = None; gps_history = []; MAX_GPS_HISTORY = 100
eye_serial = None; eye_connected = False; eye_port = None
gps_serial = None; gps_enabled = False
serial_buf = []; det_id_counter = 1
connection_lock = threading.Lock()
DATA_DIR = Path('data'); SESSION_FILE = DATA_DIR / 'eyespy_session.pkl'
DATA_DIR.mkdir(exist_ok=True)

def load_session():
    global detections
    try:
        if SESSION_FILE.exists():
            with open(SESSION_FILE,'rb') as f: saved = pickle.load(f)
            detections = saved.get('detections', [])
            print(f"[eyespy-api] loaded {len(detections)} saved detections")
    except Exception as e: print(f"[eyespy-api] load error: {e}")

def save_session():
    try:
        with open(SESSION_FILE,'wb') as f: pickle.dump({'detections': detections}, f)
    except Exception as e: print(f"[eyespy-api] save error: {e}")

def parse_nmea(sentence):
    parts = sentence.strip().split(',')
    if not parts: return None
    kind = parts[0].lstrip('$')
    try:
        if kind in ('GNGGA','GPGGA') and len(parts) >= 10:
            def dm2dd(dm, hemi):
                if not dm: return None
                d = int(float(dm)//100); m = float(dm) - d*100; v = d + m/60
                return -v if hemi in 'SW' else v
            lat = dm2dd(parts[2], parts[3]); lon = dm2dd(parts[4], parts[5])
            if lat is None or lon is None: return None
            return {'latitude': lat, 'longitude': lon,
                    'altitude': float(parts[9]) if parts[9] else None,
                    'satellites': int(parts[7]) if parts[7] else 0,
                    'fix_quality': int(parts[6]) if parts[6] else 0,
                    'timestamp': parts[1]}
    except Exception: pass
    return None

def find_gps_match(ts):
    if not gps_history: return None
    best, best_diff = None, float('inf')
    for g in gps_history:
        diff = abs(ts - g['system_timestamp'])
        if diff < best_diff and diff <= 30: best, best_diff = g, diff
    return best

_RE_SCORE  = re.compile(r'\[eyespy\] \+(\d+) \(([^)]+)\)\s+score=(\d+)')
_RE_STATUS = re.compile(r'\[eyespy\] status\s+score=(\d+)\s+(\w+)\s+phase=(\w+)\s+tracked=(\d+)')
_RE_DECAY  = re.compile(r'\[eyespy\] decay\s+score=(\d+)')
_RE_BLE    = re.compile(r'\[eyespy\] (.+?)\s+RSSI=(-?\d+)')
# NOTE the single '\s+', not '\s{2,}'. This pattern previously required TWO OR
# MORE spaces before RSSI=, which matches the CHECK_DET() emitters (they print
# `tag "  RSSI=..."`) but NOT the Raven UUID line, which prints
# `"[eyespy] Raven UUID %s RSSI=%d"` with a single space. The result was that
# every Raven detection was parsed, failed to match, and returned None — so it
# never reached the dashboard, the session or any export, with no error anywhere.
# This is the third instance of exactly this failure mode (see also the SSID
# lines and the padded right-aligned OUI lines); the firmware emitters are now
# uniformly two-space, but the pattern stays tolerant so units flashed with the
# older one-space form are still recorded.
# Firmware-default radio MAC line — note it says "MAC", not "OUI", because it is
# an exact full-address match rather than an OUI-prefix match, so neither
# _RE_WIFI nor _RE_WIFI2 (both of which require the literal word "OUI") can
# parse it. Without this regex the detection was silently dropped by the API
# entirely — it reached this parser and returned None, so nothing was ever
# recorded on the dashboard. Matched before _RE_WIFI/_RE_WIFI2.
_RE_FWMAC  = re.compile(r'\[eyespy\] (Flock-FW-default MAC) ([0-9a-fA-F:]{17})\s+"([^"]*)"')
# \s+ rather than a single space after "OUI": the firmware prints several of
# these lines with the OUI column *right-aligned* (e.g. "[eyespy] ALPR OUI      00:0e:58",
# "[eyespy] SoundThinking OUI d4:11:d6"), and the original single-space pattern
# silently failed to parse every padded one — those detections never reached the
# dashboard. Same class of bug as the missing _RE_SSID below.
_RE_WIFI   = re.compile(r'\[eyespy\] (.+?) OUI\s+([0-9a-fA-F:]{8})\s+"([^"]*)"')
_RE_WIFI2  = re.compile(r'\[eyespy\] (.+?) OUI\s+([0-9a-fA-F:]{8})')
# SSID-only lines ("[eyespy] Flock SSID \"...\"", ALPR SSID, cam SSID). These
# have no OUI to match on, so neither _RE_WIFI nor _RE_WIFI2 could parse them --
# meaning every SSID-keyword detection was silently dropped by this API and
# never reached the dashboard or the exports. Found by feeding the firmware's
# own log lines through parse_eyespy_line() and asserting none returned None.
_RE_SSID   = re.compile(r'\[eyespy\] (.+? SSID)\s+"([^"]*)"')

# ---------------------------------------------------------------------------
# Firmware-derived detection signatures  (Flock camera firmware dump,
# 2026-09-16; upstream colonelpanichacks/flock-you)
# ---------------------------------------------------------------------------
#
# The subset of the detection set extracted from a real Flock Safety ALPR camera
# firmware image (Qualcomm MSM8953 + QCA9377, Android 8.1, codename "hpnotiq"),
# as distinct from the community field-research tables. The match itself runs on
# the ESP32 (src/es_detect.h / es_confidence.h); this copy exists so the
# dashboard can label which detections rest on firmware-derived evidence.
#
# Where the tags come from, given this API parses the firmware's *text* log
# lines rather than JSON:
#   - the parsed `oui` field (the OUI prefix the firmware printed)
#   - the parsed `detection_method` string, for the paths whose specificity
#     isn't recoverable from the OUI alone (exact factory-default MAC, GATT)
#   - `ssid` when the firmware printed one
#   - BLE name / service-UUID / SDP fields, which only exist on *imported*
#     records — eye-spy's own BLE log line carries the classification but not
#     the advertised name (see the note on BLE name shapes below)
FIRMWARE_TARGET_OUIS = (
    "b4:1e:52",   # Flock Safety's own IEEE MA-L registration
    "00:03:7f",   # Qualcomm Atheros QCA9377 default-radio prefix
)

FIRMWARE_DEFAULT_MACS = (
    "00:03:7f:50:00:01",   # bdwlan30.bin / fakeboar.bin factory default
    "00:03:7f:4f:00:16",   # otp30.bin factory default
)

FIRMWARE_SSID_KEYWORDS = (
    "flock",           # "Flock-XXXXXX" SoftAP + bare "Flock"
    "penguin",         # Penguin battery pack
    "fs ext battery",  # FS Ext Battery pack
)

FIRMWARE_BLE_MFG_IDS = (0x09C8,)   # XUNTONG (Penguin pack, serial in payload)

FIRMWARE_BLE_GATT_UUIDS = (
    "e8ccbb38-9532-46a8-9fe5-1814df172e6f",  # Flock accessory service
    "00001530-1212-efde-1523-785feabcd123",  # Nordic legacy DFU service
)

FIRMWARE_RAVEN_SVC_RANGE = (0x3100, 0x3500)

# Classic-Bluetooth corroboration (same firmware dump).
#
# HOST-SIDE ONLY: the ESP32 runs NimBLE, which cannot do Classic BT at all, so
# these never arrive from our own firmware — they come from host-side tooling or
# imported captures. Tagged anyway so an imported record carries the same
# evidence labels as anything else, and because they are useful *next to* a BLE
# hit: a device advertising the MSM8953 platform's default BT name, or an SDP
# Device-ID record naming Qualcomm vendor 0x001D / product 0x1200
# (`bt_did.conf`), is the camera's Bluetooth stack showing through. Both names
# are generic platform/Android defaults, so corroborating only — never standalone.
CLASSIC_BT_DEVICE_NAMES = ("msm8953_32", "android")
CLASSIC_BT_SDP_DEVICE_ID = {
    "vendor_id": 0x001D,   # Qualcomm
    "product_id": 0x1200,
}

_BLE_NAME_PATTERNS = (
    (re.compile(r"^penguin-\d{10}$", re.IGNORECASE), "ble_name:penguin_serial"),
    (re.compile(r"^\d{10}$"), "ble_name:bare_serial"),
    (re.compile(r"^fs ext battery$", re.IGNORECASE), "ble_name:fs_ext_battery"),
    (re.compile(r"^dfutarg$", re.IGNORECASE), "ble_name:dfutarg"),
)

# firmware detection_method string -> signature tag. Only for paths whose
# specificity the parsed `oui` field cannot convey. Keys are the strings this API
# actually ends up with after parsing, lower-cased — note the OUI lines parse to
# the prefix WITHOUT "OUI" ("Flock-cam", "Flock-mfr", "ALPR", "SoundThinking",
# "cam"), because _RE_WIFI captures everything before the literal " OUI ".
#
# Deliberately NO "flock-mfr" entry: that method fires for every mfr-tier OUI
# (e0:0a:f6, f4:6a:dd, …), so mapping it to "oui:00:03:7f" would tag unrelated
# OUIs as firmware-derived. The genuine 00:03:7f case is tagged from the parsed
# `oui` field instead, which is exact.
_METHOD_TAGS = {
    # The API receives the full address for this one (_RE_FWMAC captures it), so
    # the mac check below also fires; this entry is belt-and-braces for anyone
    # replaying a line whose MAC formatting differs.
    "flock-fw-default mac": "mac:fw_default",
    # The firmware's Flock-GATT detector matches EITHER the Flock accessory
    # service or the Nordic DFU service, and the log line carries no UUID — so
    # this tag means "a firmware-derived Flock GATT service matched", not
    # specifically the accessory one.
    "flock-gatt":           "gatt:flock_accessory",
    "flock-ble-mfrid":      "ble_mfg:0x09c8",
    "raven-ble-uuid":       "gatt:raven_service",
}

def _parse_int_flexible(value):
    """Accept an int, a decimal string ("2504") or a hex string ("0x09c8")."""
    if value is None:
        return None
    if isinstance(value, int):
        return value
    text = str(value).strip()
    for base in (0, 16):
        try:
            return int(text, base)
        except (ValueError, TypeError):
            continue
    return None

def _normalized_mac(value) -> str:
    """Lower-case colon-separated MAC, tolerating dashes and None."""
    if not value:
        return ""
    return str(value).replace("-", ":").strip().lower()

def _gatt_signatures(uuids) -> list:
    """Signature tags for advertised GATT service UUIDs."""
    tags = []
    for raw in uuids:
        text = str(raw).strip().lower()
        if text in FIRMWARE_BLE_GATT_UUIDS:
            tags.append("gatt:flock_accessory" if "e8ccbb38" in text else "gatt:nordic_dfu")
            continue
        # 16-bit service: "0x3101"/"3101", or the canonical 128-bit expansion
        # NimBLE emits. The value is the LOW half of the first 32-bit group —
        # "00003101-..." is 0x3101, not 0x0000. (The upstream Python equivalent
        # captured the high half, so it never matched the canonical form.)
        value = _parse_int_flexible(text)
        if value is None:
            m = re.match(r"^([0-9a-f]{8})-0000-1000-8000-00805f9b34fb$", text)
            if m:
                value = int(m.group(1), 16) & 0xFFFF
        if (value is not None
                and FIRMWARE_RAVEN_SVC_RANGE[0] <= value <= FIRMWARE_RAVEN_SVC_RANGE[1]):
            tags.append(f"gatt:raven_service:0x{value:04x}")
    return tags


def firmware_signature_matches(data: dict) -> list:
    """Firmware-derived signature tags for one detection record.

    Returns tags such as "oui:b4:1e:52", "mac:fw_default",
    "ssid_keyword:penguin" or "classic_bt_name:msm8953_32". Empty list when
    nothing in the firmware-derived set matched.
    """
    if not isinstance(data, dict):
        return []
    tags = []

    oui = str(data.get("oui") or "").strip().lower()
    if oui in FIRMWARE_TARGET_OUIS:
        tags.append(f"oui:{oui}")

    if _normalized_mac(data.get("mac_address")) in FIRMWARE_DEFAULT_MACS:
        tags.append("mac:fw_default")

    method_tag = _METHOD_TAGS.get(str(data.get("detection_method") or "").strip().lower())
    if method_tag:
        tags.append(method_tag)

    ssid = data.get("ssid")
    if ssid:
        lowered = str(ssid).lower()
        for keyword in FIRMWARE_SSID_KEYWORDS:
            if keyword in lowered:
                tags.append(f"ssid_keyword:{keyword}")

    name = data.get("device_name") or data.get("name")
    if name:
        text = str(name).strip()
        for pattern, tag in _BLE_NAME_PATTERNS:
            if pattern.match(text):
                tags.append(tag)
                break
        lowered_name = text.lower()
        for keyword in ("penguin", "fs ext battery", "dfutarg"):
            if keyword in lowered_name:
                tags.append(f"ble_name:{keyword.replace(' ', '_')}")
        # Classic-Bluetooth corroboration (host-side / imported only — see the
        # CLASSIC_BT_* constant comments).
        if lowered_name in CLASSIC_BT_DEVICE_NAMES:
            tags.append(f"classic_bt_name:{lowered_name}")

    vendor = _parse_int_flexible(data.get("sdp_vendor_id", data.get("vendor_id")))
    product = _parse_int_flexible(data.get("sdp_product_id", data.get("product_id")))
    if (vendor == CLASSIC_BT_SDP_DEVICE_ID["vendor_id"]
            and product == CLASSIC_BT_SDP_DEVICE_ID["product_id"]):
        tags.append("classic_bt_sdp_did:qualcomm_001d_1200")

    company = _parse_int_flexible(
        data.get("company_id") or data.get("mfg_company_id")
        or data.get("manufacturer_company_id"))
    if company is not None and company in FIRMWARE_BLE_MFG_IDS:
        tags.append(f"ble_mfg:0x{company:04x}")

    uuids = []
    for key in ("service_uuids", "gatt_services", "service_uuid"):
        value = data.get(key)
        if isinstance(value, (list, tuple)):
            uuids.extend(value)
        elif value:
            uuids.append(value)
    tags.extend(_gatt_signatures(uuids))

    # Dedupe, preserving order (a UUID list can repeat a service).
    seen = set()
    unique = []
    for tag in tags:
        if tag not in seen:
            seen.add(tag)
            unique.append(tag)
    return unique

def tag_firmware_signatures(data: dict) -> dict:
    """Merge firmware-derived tags onto a detection dict, in place.

    Sets two additive fields (existing fields are untouched):
      matched_signatures — ordered union of every firmware-derived signature
                           tag that hit.
      firmware_sig       — True when at least one such signature matched.
    """
    matched = list(data.get("matched_signatures") or [])
    for tag in firmware_signature_matches(data):
        if tag not in matched:
            matched.append(tag)
    data["matched_signatures"] = matched
    data["firmware_sig"] = bool(matched)
    return data


def parse_eyespy_line(line):
    global g_score, g_status, g_phase, g_tracked
    m = _RE_STATUS.match(line)
    if m:
        g_score=int(m.group(1)); g_status=m.group(2); g_phase=m.group(3); g_tracked=int(m.group(4))
        socketio.emit('score_update', {'score':g_score,'status':g_status,'phase':g_phase,'tracked':g_tracked})
        return None
    m = _RE_DECAY.match(line)
    if m:
        g_score=int(m.group(1))
        socketio.emit('score_update', {'score':g_score,'status':g_status,'phase':g_phase,'tracked':g_tracked})
        return None
    m = _RE_SCORE.match(line)
    if m:
        pts=int(m.group(1)); method=m.group(2); g_score=int(m.group(3))
        status='ALERT' if g_score>=6 else 'CAUTION' if g_score>=3 else 'CLEAR'
        socketio.emit('score_update', {'score':g_score,'status':status,'phase':g_phase,'tracked':g_tracked})
        return {'detection_type':'score','detection_method':method,'protocol':'multi','points':pts,'score_after':g_score}
    m = _RE_FWMAC.match(line)
    if m: return {'detection_type':'wifi','detection_method':m.group(1),'protocol':'wifi',
                  # Unlike the OUI rows below, the *full* address is known here —
                  # the firmware matched the whole 6-byte factory default — so
                  # store it as-is rather than a fabricated ":xx:xx:xx" tail.
                  'mac_address':m.group(2),'oui':m.group(2)[:8],'ssid':m.group(3),'rssi':None}
    m = _RE_WIFI.match(line)
    if m: return {'detection_type':'wifi','detection_method':m.group(1),'protocol':'wifi',
                  'mac_address':m.group(2)+':xx:xx:xx','oui':m.group(2),'ssid':m.group(3),'rssi':None}
    m = _RE_WIFI2.match(line)
    if m: return {'detection_type':'wifi','detection_method':m.group(1),'protocol':'wifi',
                  'mac_address':m.group(2)+':xx:xx:xx','oui':m.group(2),'ssid':'','rssi':None}
    # SSID-only match: no MAC is known (the firmware only matched on the SSID
    # keyword), so mac_address/oui stay None — same convention the BLE rows use.
    m = _RE_SSID.match(line)
    if m: return {'detection_type':'wifi','detection_method':m.group(1),'protocol':'wifi',
                  'mac_address':None,'oui':None,'ssid':m.group(2),'rssi':None}
    m = _RE_BLE.match(line)
    if m: return {'detection_type':'ble','detection_method':m.group(1),'protocol':'ble',
                  'mac_address':None,'rssi':int(m.group(2))}
    return None

def add_detection(data):
    global det_id_counter, detections
    # Tag firmware-derived signature hits, unioned onto anything already on the
    # record (see the signature block above). Every ingest path funnels through
    # here, so live serial lines and imported records get identical tags.
    tag_firmware_signatures(data)
    now = time.time()
    data['id'] = det_id_counter; det_id_counter += 1
    data['timestamp'] = datetime.fromtimestamp(now).isoformat()
    data['detection_time'] = datetime.fromtimestamp(now).strftime('%Y-%m-%d %H:%M:%S')
    data['score_at_time'] = g_score; data['alert_level'] = g_status
    best = find_gps_match(now)
    if best and best.get('fix_quality',0) > 0:
        data['gps'] = {k: best.get(k) for k in ('latitude','longitude','altitude','satellites','fix_quality')}
        data['gps']['time_diff'] = abs(now - best['system_timestamp'])
    elif gps_data and gps_data.get('fix_quality',0) > 0:
        data['gps'] = {k: gps_data.get(k) for k in ('latitude','longitude','altitude','satellites','fix_quality')}
        data['gps']['time_diff'] = None
    else:
        data['gps'] = None
    detections.append(data); socketio.emit('new_detection', data); save_session()

def safe_emit(event, data, room=None):
    try:
        if room: socketio.emit(event, data, room=room)
        else: socketio.emit(event, data)
    except Exception as e: print(f"[eyespy-api] emit error {event}: {e}")

def eye_reader():
    global eye_serial, eye_connected, serial_buf
    with app.app_context():
        while eye_connected:
            if eye_serial and eye_serial.is_open:
                try:
                    line = eye_serial.readline().decode('utf-8', errors='ignore').strip()
                    if line:
                        serial_buf.append(line)
                        if len(serial_buf) > 500: serial_buf.pop(0)
                        safe_emit('serial_data', line, room='terminal')
                        det = parse_eyespy_line(line)
                        if det and det.get('detection_type') in ('wifi','ble'): add_detection(det)
                except Exception as e:
                    print(f"[eyespy-api] eye reader error: {e}")
                    with connection_lock: eye_connected = False
                    safe_emit('device_disconnected', {}); break
            time.sleep(0.05)

def gps_reader():
    global gps_serial, gps_enabled, gps_data, gps_history
    while gps_enabled:
        if gps_serial and gps_serial.is_open:
            try:
                line = gps_serial.readline().decode('utf-8', errors='ignore')
                if line:
                    parsed = parse_nmea(line)
                    if parsed:
                        gps_data = parsed
                        if parsed.get('fix_quality',0) > 0:
                            entry = parsed.copy(); entry['system_timestamp'] = time.time()
                            gps_history.append(entry)
                            if len(gps_history) > MAX_GPS_HISTORY: gps_history.pop(0)
                        safe_emit('gps_update', parsed)
            except Exception as e:
                print(f"[eyespy-api] gps reader error: {e}")
                with connection_lock: gps_enabled = False
                safe_emit('gps_disconnected', {}); break
        time.sleep(0.1)

@app.route('/')
def index(): return render_template('index.html')

@app.route('/api/status')
def api_status():
    return jsonify({'device_connected':eye_connected,'device_port':eye_port,'gps_enabled':gps_enabled,
                    'score':g_score,'status':g_status,'phase':g_phase,'tracked':g_tracked,'detection_count':len(detections)})

@app.route('/api/detections')
def api_detections(): return jsonify(detections)

@app.route('/api/serial/ports')
def api_ports():
    return jsonify([{'port':p.device,'description':p.description} for p in serial.tools.list_ports.comports()])

@app.route('/api/device/connect', methods=['POST'])
def connect_device():
    global eye_serial, eye_connected, eye_port
    port = request.json.get('port')
    if not port: return jsonify({'status':'error','message':'No port'}), 400
    try:
        if eye_serial and eye_serial.is_open: eye_serial.close()
        eye_serial = serial.Serial(port, 115200, timeout=1)
        eye_port, eye_connected = port, True
        threading.Thread(target=eye_reader, daemon=True).start()
        return jsonify({'status':'ok','port':port})
    except Exception as e: return jsonify({'status':'error','message':str(e)}), 500

@app.route('/api/device/disconnect', methods=['POST'])
def disconnect_device():
    global eye_serial, eye_connected, eye_port
    eye_connected = False; eye_port = None
    if eye_serial and eye_serial.is_open: eye_serial.close()
    return jsonify({'status':'ok'})

@app.route('/api/gps/connect', methods=['POST'])
def connect_gps():
    global gps_serial, gps_enabled
    port = request.json.get('port'); baud = request.json.get('baud', 9600)
    if not port: return jsonify({'status':'error','message':'No port'}), 400
    try:
        if gps_serial and gps_serial.is_open: gps_serial.close()
        gps_serial = serial.Serial(port, baud, timeout=1); gps_enabled = True
        threading.Thread(target=gps_reader, daemon=True).start()
        return jsonify({'status':'ok','port':port})
    except Exception as e: return jsonify({'status':'error','message':str(e)}), 500

@app.route('/api/gps/disconnect', methods=['POST'])
def disconnect_gps():
    global gps_serial, gps_enabled
    gps_enabled = False
    if gps_serial and gps_serial.is_open: gps_serial.close()
    return jsonify({'status':'ok'})

@app.route('/api/export/json')
def export_json():
    buf = io.BytesIO(json.dumps(detections, indent=2).encode()); buf.seek(0)
    return send_file(buf, mimetype='application/json', as_attachment=True,
                     download_name=f"eyespy_{datetime.now().strftime('%Y%m%d_%H%M%S')}.json")

@app.route('/api/export/csv')
def export_csv():
    buf = io.StringIO()
    fields = ['id','detection_time','detection_type','detection_method','protocol',
              'mac_address','oui','ssid','rssi','score_at_time','alert_level',
              'firmware_sig','matched_signatures',
              'gps.latitude','gps.longitude','gps.altitude','gps.satellites']
    w = csv.DictWriter(buf, fieldnames=fields, extrasaction='ignore'); w.writeheader()
    for d in detections:
        row = {k: d.get(k,'') for k in fields}
        # matched_signatures is a list — join it so the cell stays readable.
        row['matched_signatures'] = '; '.join(d.get('matched_signatures') or [])
        gps = d.get('gps') or {}
        for gk in ('latitude','longitude','altitude','satellites'): row['gps.'+gk] = gps.get(gk,'')
        w.writerow(row)
    buf.seek(0)
    return send_file(io.BytesIO(buf.getvalue().encode()), mimetype='text/csv', as_attachment=True,
                     download_name=f"eyespy_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv")

@app.route('/api/export/kml')
def export_kml():
    pms = ''
    for d in detections:
        g = d.get('gps')
        if not g or not g.get('latitude'): continue
        n = d.get('detection_method','unknown'); lv = d.get('alert_level','?')
        mac = d.get('mac_address','?'); rssi = d.get('rssi','?')
        pms += ('  <Placemark><name>' + n + '</name>'
                '<description>Level: ' + lv + ' | MAC: ' + str(mac) + ' | RSSI: ' + str(rssi) + '</description>'
                '<Point><coordinates>' + str(g['longitude']) + ',' + str(g['latitude']) + ','
                + str(g.get('altitude',0) or 0) + '</coordinates></Point></Placemark>\n')
    kml = ('<?xml version="1.0" encoding="UTF-8"?>'
           '<kml xmlns="http://www.opengis.net/kml/2.2"><Document>'
           '<name>Eye Spy Detections</name>' + pms + '</Document></kml>')
    buf = io.BytesIO(kml.encode())
    return send_file(buf, mimetype='application/vnd.google-earth.kml+xml', as_attachment=True,
                     download_name=f"eyespy_{datetime.now().strftime('%Y%m%d_%H%M%S')}.kml")

@app.route('/api/clear', methods=['POST'])
def clear_detections():
    global detections, g_score
    detections = []; g_score = 0
    if SESSION_FILE.exists(): SESSION_FILE.unlink()
    return jsonify({'status':'ok'})

@socketio.on('connect')
def on_connect(): pass

@socketio.on('join_terminal')
def on_join_terminal():
    join_room('terminal')
    for line in serial_buf[-50:]: emit('serial_data', line)

if __name__ == '__main__':
    load_session()
    print("Eye Spy API server starting on http://localhost:5001")
    try:
        socketio.run(app, debug=False, host='0.0.0.0', port=5001, allow_unsafe_werkzeug=True)
    except KeyboardInterrupt:
        print("\nShutting down...")
        if eye_serial and eye_serial.is_open: eye_serial.close()
        if gps_serial and gps_serial.is_open: gps_serial.close()
