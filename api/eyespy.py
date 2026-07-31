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
_RE_BLE    = re.compile(r'\[eyespy\] (.+?)\s{2,}RSSI=(-?\d+)')
_RE_WIFI   = re.compile(r'\[eyespy\] (.+?) OUI ([0-9a-fA-F:]{8})\s+"([^"]*)"')
_RE_WIFI2  = re.compile(r'\[eyespy\] (.+?) OUI ([0-9a-fA-F:]{8})')

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
    m = _RE_WIFI.match(line)
    if m: return {'detection_type':'wifi','detection_method':m.group(1),'protocol':'wifi',
                  'mac_address':m.group(2)+':xx:xx:xx','oui':m.group(2),'ssid':m.group(3),'rssi':None}
    m = _RE_WIFI2.match(line)
    if m: return {'detection_type':'wifi','detection_method':m.group(1),'protocol':'wifi',
                  'mac_address':m.group(2)+':xx:xx:xx','oui':m.group(2),'ssid':'','rssi':None}
    m = _RE_BLE.match(line)
    if m: return {'detection_type':'ble','detection_method':m.group(1),'protocol':'ble',
                  'mac_address':None,'rssi':int(m.group(2))}
    return None

def add_detection(data):
    global det_id_counter, detections
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
              'gps.latitude','gps.longitude','gps.altitude','gps.satellites']
    w = csv.DictWriter(buf, fieldnames=fields, extrasaction='ignore'); w.writeheader()
    for d in detections:
        row = {k: d.get(k,'') for k in fields}
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
