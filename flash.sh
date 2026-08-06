#!/usr/bin/env bash
# ────────────────────────────────────────────────────────────────────────────
# flash.sh — Eye Spy unified firmware flasher
#
# Supported devices (/dev/cu.usbserial-* or /dev/cu.usbmodem*):
#   1) Generic ESP32 DevKit         — WROOM/WROVER breakout
#   2) M5Stack ATOM Lite            — RGB LED (FTDI)
#   3) M5Stack ATOM Echo            — speaker + microphone (FTDI)
#   4) M5Stack ATOM Voice           — I2S speaker (FTDI)
#   5) M5Stack Basic Core v2.7      — 2.0" IPS display + speaker (CH9102)
#   6) M5Stack Core2 For AWS        — 2.0" capacitive touch + I2S + PSRAM (CH9102)
#   7) M5StickC Plus SE             — 1.14" display + passive buzzer (FTDI)
#   8) LILYGO T-Dongle C5           — USB-C dongle, TFT + RGB LED (experimental)
#
# Usage:
#   ./flash.sh          Flash connected device, then loop for the next one
#   ./flash.sh --once   Flash one device and exit
# ────────────────────────────────────────────────────────────────────────────

set -o pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
LOOP=true
[[ "${1}" == "--once" ]] && LOOP=false

# ── Venv auto-load ─────────────────────────────────────────────────────────────
if [[ -z "$VIRTUAL_ENV" ]]; then
    for candidate in \
        "$SCRIPT_DIR/.venv"  \
        "$SCRIPT_DIR/venv"   \
        "$SCRIPT_DIR/env"    \
        "$HOME/.platformio/penv"
    do
        [[ -f "$candidate/bin/activate" ]] && source "$candidate/bin/activate" && break
    done
fi

cd "$SCRIPT_DIR"

FLASHED_MACS=()

# ── Post-flash boot monitor ────────────────────────────────────────────────────
show_boot_output() {
    local port="$1"
    local timeout_s="${2:-30}"

    local deadline=$(( $(date +%s) + 5 ))
    while [[ ! -c "$port" ]] && (( $(date +%s) < deadline )); do sleep 0.2; done

    if [[ ! -c "$port" ]]; then
        echo "   ⚠️  Boot monitor: port $port not available — skipping"
        return
    fi

    echo ""
    echo "📡 Boot output from $(basename "$port") (waiting for firmware, Ctrl-C skips):"
    echo "────────────────────────────────────────────────────────────────"

    exec 3<>"$port" 2>/dev/null || { echo "   ⚠️  Could not open $port"; return; }
    stty -f "$port" 115200 raw cs8 -cstopb -parenb clocal -hupcl 2>/dev/null

    local start_ts=$(date +%s)
    local seen_eyespy=0

    while (( $(date +%s) - start_ts < timeout_s )); do
        if IFS= read -r -t 2 line <&3 2>/dev/null; then
            line="${line%$'\r'}"
            [[ -n "$line" ]] && printf "   \033[2m%s\033[0m\n" "$line"
            [[ "$line" == *"[eyespy]"* ]] && seen_eyespy=1
            if [[ "$line" == *"[eyespy] init OK"* ]] || \
               [[ $seen_eyespy -eq 1 && "$line" == *"status"* ]]; then
                echo "────────────────────────────────────────────────────────────────"
                echo "✅ Eye Spy firmware confirmed running."
                exec 3>&- 2>/dev/null; return
            fi
        fi
    done

    exec 3>&- 2>/dev/null
    echo "────────────────────────────────────────────────────────────────"
    if [[ $seen_eyespy -eq 1 ]]; then
        echo "✅ Firmware running."
    else
        echo "⚠️  No firmware output in ${timeout_s}s — check USB connection or reboot manually."
    fi
}

mac_already_flashed() {
    local m="$1"
    local e
    for e in "${FLASHED_MACS[@]+"${FLASHED_MACS[@]}"}"; do
        [[ "$e" == "$m" ]] && return 0
    done
    return 1
}

# ── Main loop ──────────────────────────────────────────────────────────────────
while true; do
    echo ""
    echo "🔍 Scanning for devices..."

    SERIAL_PORTS=$(ls /dev/cu.usbserial-* 2>/dev/null || true)
    MODEM_PORTS=$(ls /dev/cu.usbmodem*    2>/dev/null || true)
    ALL_PORTS=""
    [[ -n "$SERIAL_PORTS" ]] && ALL_PORTS="$SERIAL_PORTS"
    [[ -n "$MODEM_PORTS"  ]] && ALL_PORTS="${ALL_PORTS}${ALL_PORTS:+ }$MODEM_PORTS"

    if [[ -z "$ALL_PORTS" ]]; then
        echo "❌ No device found."
        echo "   → Check USB cable (many USB-C cables are charge-only)"
        echo "   → Expected: /dev/cu.usbserial-* or /dev/cu.usbmodem*"
        if $LOOP; then
            read -r -p "   Press Enter to retry, or Ctrl-C to quit… "
            continue
        else
            exit 1
        fi
    fi

    PORT_COUNT=$(echo "$ALL_PORTS" | wc -w | tr -d ' ')
    if [[ "$PORT_COUNT" -gt 1 ]]; then
        echo "⚠️  Multiple ports found. Select one:"
        select PORT in $ALL_PORTS; do [[ -n "$PORT" ]] && break; done
    else
        PORT=$(echo "$ALL_PORTS" | tr -d ' ')
    fi

    # ── Device selection menu ─────────────────────────────────────────────────
    echo ""
    echo "🔌 Device at $PORT — what is it?"
    echo ""
    echo "   1) Generic ESP32 DevKit       — WROOM/WROVER breakout board"
    echo "   2) M5Stack ATOM Lite          — RGB LED, BLE + WiFi scan (FTDI)"
    echo "   3) M5Stack ATOM Echo          — speaker + microphone (FTDI)"
    echo "   4) M5Stack ATOM Voice         — I2S speaker (FTDI)"
    echo "   5) M5Stack Basic Core v2.7    — 2.0\" IPS display + speaker (CH9102)"
    echo "   6) M5Stack Core2 For AWS      — 2.0\" capacitive touch + I2S (CH9102)"
    echo "   7) M5StickC Plus SE           — 1.14\" display + passive buzzer (FTDI)"
    echo "   8) LILYGO T-Dongle C5         — TFT + RGB dongle (ESP32-C5, experimental)"
    echo ""
    read -r -p "   Enter 1–8 (default: 1): " VARIANT

    case "$VARIANT" in
        1|"") ENV="esp32dev"           ; LABEL="Generic ESP32 DevKit"       ;;
        2)    ENV="atom-lite"          ; LABEL="M5Atom Lite"                ;;
        3)    ENV="atom-echo"          ; LABEL="M5Atom Echo"                ;;
        4)    ENV="atom-voice"         ; LABEL="M5Atom Voice"               ;;
        5)    ENV="m5stack-basic"      ; LABEL="M5Stack Basic Core v2.7"    ;;
        6)    ENV="m5stack-core2-aws"  ; LABEL="M5Stack Core2 For AWS"      ;;
        7)    ENV="m5stickc-plus-se"   ; LABEL="M5StickC Plus SE"           ;;
        8)    ENV="lilygo-t-dongle-c5" ; LABEL="LILYGO T-Dongle C5 (experimental)" ;;
        *)    ENV="esp32dev"           ; LABEL="Generic ESP32 DevKit"       ;;
    esac

    echo ""
    echo "✅ Selected: $LABEL"

    # ── Read device MAC for duplicate detection ───────────────────────────────
    MAC=$(esptool.py --port "$PORT" --no-stub chip_id 2>/dev/null \
          | grep -i "MAC:" | awk '{print $2}' || echo "unknown")

    if [[ -n "$MAC" && "$MAC" != "unknown" ]] && mac_already_flashed "$MAC"; then
        echo "⚠️  Already flashed this device ($MAC) — unplug it and plug in the next one."
        if $LOOP; then
            read -r -p "   Press Enter when ready… "
            continue
        else
            exit 0
        fi
    fi

    echo "   Flashing $LABEL at $PORT  (MAC: ${MAC:-n/a})"

    # ── Flash ─────────────────────────────────────────────────────────────────
    echo ""
    echo "⬆️  Flashing [$ENV] → $PORT"
    echo "────────────────────────────────────"

    if pio run -e "$ENV" -t upload --upload-port "$PORT"; then
        [[ -n "$MAC" && "$MAC" != "unknown" ]] && FLASHED_MACS+=("$MAC")
        echo ""
        echo "✅ Flash complete! ($LABEL)"
        show_boot_output "$PORT" 30
    else
        echo ""
        echo "❌ Flash FAILED. Check:"
        echo "   • Is the correct board selected? (option $VARIANT = $LABEL)"
        echo "   • Is the USB cable a data cable (not charge-only)?"
        echo "   • Try unplugging and re-plugging, then flash again."
        if $LOOP; then
            read -r -p "   Press Enter to try again or Ctrl-C to quit… "
            continue
        else
            exit 1
        fi
    fi

    if $LOOP; then
        echo ""
        read -r -p "🔁 Plug in the next device, then press Enter… (Ctrl-C to stop) "
    else
        break
    fi
done

echo ""
echo "🎉 All done! Flashed ${#FLASHED_MACS[@]} device(s) this session."
