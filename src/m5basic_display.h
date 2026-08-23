// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2024 SimeonOnSecurity <https://github.com/simeononsecurity>
//
// m5basic_display.h — M5Stack Basic Core v2.7 display for Eye Spy
//
// Hardware (Basic v2.7 schematic):
//   ILI9342C 320×240 IPS — managed by M5Unified (LovyanGFX)
//   Speaker 1W on G25    — M5.Speaker.tone()
//   Button A: G39  Button B: G38  Button C: G37
//
// Display layout (320×240 landscape):
//   Row 0    — header bar: "EYE SPY  SCORE:N  <status>"
//   Row 18   — main content area
//   Row 210  — separator
//   Row 214  — button label bar [A] [B] [C]
//
// Button actions (eye-spy):
//   A — reset score to 0 (manual clear)
//   B — cycle display brightness
//   C — force immediate scan cycle restart
//
// Screen states:
//   CLEAR (0-2)   → green header, scanning info, tracked device count
//   CAUTION (3-5) → amber header, last detection type, RSSI, time since
//   ALERT (6+)    → red header, flashing alert, last detection, time since
#pragma once
#if defined(USE_M5BASIC)

#include <M5Unified.h>
#include <cstring>
#include <cstdio>
#include <cmath>


// ── RGB565 palette ────────────────────────────────────────────────────────────
static constexpr uint16_t MBE_BLACK    = 0x0000;
static constexpr uint16_t MBE_WHITE    = 0xFFFF;
static constexpr uint16_t MBE_RED      = 0xF800;
static constexpr uint16_t MBE_GREEN    = 0x07E0;
static constexpr uint16_t MBE_BLUE     = 0x001F;
static constexpr uint16_t MBE_YELLOW   = 0xFFE0;
static constexpr uint16_t MBE_CYAN     = 0x07FF;
static constexpr uint16_t MBE_ORANGE   = 0xFD20;
static constexpr uint16_t MBE_DARK_RED = 0x8000;
static constexpr uint16_t MBE_DARK_AMB = 0x8280;
static constexpr uint16_t MBE_DARK_GRN = 0x0320;
static constexpr uint16_t MBE_GREY     = 0x8410;
static constexpr uint16_t MBE_LT_GREY  = 0xC618;
static constexpr uint16_t MBE_DK_GREY  = 0x2104;

// ── Layout ────────────────────────────────────────────────────────────────────
static constexpr int MBE_W      = 320;
static constexpr int MBE_H      = 240;
static constexpr int MBE_HDR_H  = 18;
static constexpr int MBE_BTN_Y  = 214;
static constexpr int MBE_BTN_H  = 26;
// Log-strip height: 4 lines * 7px + 3px top pad + 2px margin = 33. Grown from
// a hardcoded 24 (3 lines) to mirror flock-you-esp32's equivalent log-window
// enlargement fix. Grown more conservatively here (3->4, not 3->5) because
// this board's content area is an exact zero-slack fit in its unmodified
// layout (see the spacing-reduction comment in m5basicUpdate()) — a 5-line
// strip would require trimming more than can be done safely.
static constexpr int MBE_LOG_H = 33;

// ── State ─────────────────────────────────────────────────────────────────────
static uint8_t mbe_brightness   = 160;
static int     mbe_lastScore    = -999;
static char    mbe_lastDet[32]  = {0};
static int8_t  mbe_lastRssi     = -100;
static char    mbe_lastPhase[12]= {0};
static bool    mbe_needsRedraw  = true;
// Alternate display mode toggled by Btn B (brightness cycle skipped — see below)
// mbe_altMode: 0 = main view, 1 = detection history placeholder
static uint8_t mbe_altMode      = 0;
// y-coordinate of the "Last alert: X ago" line from the most recent FULL
// redraw. Only the dedicated UI task (ui_task.h) ever reads/writes this — see
// that file's thread-safety contract — so no mutex is needed. Lets a
// stale-only tick (see m5basicUpdate()) redraw just this one time-based line
// at its known-correct position instead of paying for a full content
// fillRect(BLACK)+redraw; it's safe to reuse because dataChanged==false
// guarantees the layout is identical to the last full redraw.
static int mbe_lastAlertY = 0;

// Timestamp of the last actual redraw — used to force a periodic (~1 Hz)
// repaint even when score/lastDet/phase/lastRssi are unchanged, so the
// "Last alert: X ago" clock visibly ticks instead of appearing frozen
// between phase transitions (BLE→WIFI→PROMISC, which can be many seconds apart).
static unsigned long mbe_lastDrawMs = 0;

// ── Core2 For AWS: non-blocking vibration state machine ───────────────────────
// Previously the alert vibration used delay() directly inside m5basicUpdate(),
// which blocks the ENTIRE loop() — button polling, screen redraws, BLE/WiFi
// scan phase transitions — for up to ~1.15s per threshold crossing.  This
// tick-based state machine reproduces the identical on/off pulse pattern
// using millis() timing instead of delay(), so m5basicVibrationTick() (called
// every loop() iteration) never blocks anything.
#if defined(USE_M5CORE2_AWS)
static uint8_t       mbe_vibPattern  = 0;      // 0=idle 1=alert(2x strong) 2=caution(1x med)
static uint8_t       mbe_vibStep     = 0;      // pulse index within the pattern
static bool          mbe_vibOn       = false;  // true while motor is currently energised
static unsigned long mbe_vibNextMs   = 0;      // millis() timestamp of next state change
#endif

// ── Serial-mirror log strip ───────────────────────────────────────────────────
// Shows the last few lines of key [eyespy] status/detection text directly on
// the screen so an operator can see live activity without a USB-serial
// console attached.  Fed by mbe_logAdd(), called from a couple of high-value
// call sites in main.cpp (printStatus() + the per-engine detection macro).
// Grown 3->4 lines alongside MBE_LOG_H above.
#define MBE_LOG_LINES    4
#define MBE_LOG_LINE_LEN 53   // ~320px / 6px-per-char at text size 1
static char mbe_logBuf[MBE_LOG_LINES][MBE_LOG_LINE_LEN];
// Bumped on every mbe_logAdd() call so mbe_drawLogStrip() can skip redrawing
// the strip when nothing new has arrived (e.g. on a stale-only UI tick) —
// mirrors flock-you-esp32's mb_logVersion fix for the same root-cause bug
// (mbe_drawLogStrip() used to unconditionally fillRect(BLACK)+redraw on
// EVERY call, including harmless ~250ms stale ticks with no new log lines).
static volatile uint32_t mbe_logVersion = 0;


// mbe_logAdd() is called from scan/main-task code (printStatus(),
// IF_M5BASIC_LOG in main.cpp) while mbe_drawLogStrip() is called from the
// dedicated UI task (ui_task.h) — mbe_logBuf is therefore genuine mutable
// state shared across two FreeRTOS tasks. Guard both the writer and reader
// with a small dedicated critical section (cheap: worst case a handful of
// memcpy's of ~53 bytes each, never held across a display/SPI call).
static portMUX_TYPE mbe_logMux = portMUX_INITIALIZER_UNLOCKED;

// Appends text to the on-screen log ring buffer.  Splits on embedded '\n' so
// a single call — which may itself contain a trailing/embedded newline —
// becomes one or more ring entries, newest first.
static void mbe_logAdd(const char* text) {
    if (!text || !text[0]) return;
    const char* p = text;
    while (*p) {
        const char* nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        if (len > 0) {
            size_t n = (len < (size_t)(MBE_LOG_LINE_LEN - 1)) ? len : (size_t)(MBE_LOG_LINE_LEN - 1);
            portENTER_CRITICAL(&mbe_logMux);
            for (int i = MBE_LOG_LINES - 1; i > 0; i--)
                memcpy(mbe_logBuf[i], mbe_logBuf[i - 1], MBE_LOG_LINE_LEN);
            memcpy(mbe_logBuf[0], p, n);
            mbe_logBuf[0][n] = '\0';
            mbe_logVersion++;
            portEXIT_CRITICAL(&mbe_logMux);
        }
        if (!nl) break;
        p = nl + 1;
    }
}


// ── Internal helpers ──────────────────────────────────────────────────────────


static void mbe_fmtMs(unsigned long ms, char* buf, size_t len) {
    unsigned long s = ms / 1000;
    unsigned long m = s / 60;  s %= 60;
    unsigned long h = m / 60;  m %= 60;
    if (h > 0) snprintf(buf, len, "%lu:%02lu:%02lu", h, m, s);
    else        snprintf(buf, len, "%lu:%02lu", m, s);
}

// Score level: 0=clear 1=caution 2=alert
static int mbe_level(int score) {
    return (score >= 6) ? 2 : (score >= 3) ? 1 : 0;
}

static uint16_t mbe_levelBg(int lvl) {
    return (lvl == 2) ? MBE_DARK_RED :
           (lvl == 1) ? MBE_DARK_AMB : MBE_DARK_GRN;
}
static uint16_t mbe_levelFg(int lvl) {
    return (lvl == 2) ? MBE_RED :
           (lvl == 1) ? MBE_ORANGE : MBE_GREEN;
}
static const char* mbe_levelLabel(int lvl) {
    return (lvl == 2) ? "ALERT" :
           (lvl == 1) ? "CAUTION" : "CLEAR";
}

static void mbe_hline(int y, uint16_t col = MBE_GREY) {
    M5.Display.drawFastHLine(0, y, MBE_W, col);
}

// Full-width header bar
static void mbe_header(const char* left, const char* right,
                       uint16_t bg, uint16_t fg) {
    M5.Display.fillRect(0, 0, MBE_W, MBE_HDR_H, bg);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(fg, bg);
    M5.Display.setCursor(4, 5);
    M5.Display.print(left);
    if (right && right[0]) {
        int rw = (int)strlen(right) * 6;
        M5.Display.setCursor(MBE_W - rw - 4, 5);
        M5.Display.print(right);
    }
}

// Button label bar
static void mbe_btnBar(const char* a, const char* b, const char* c) {
    M5.Display.fillRect(0, MBE_BTN_Y, MBE_W, MBE_BTN_H, MBE_DK_GREY);
    mbe_hline(MBE_BTN_Y, MBE_GREY);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(MBE_LT_GREY, MBE_DK_GREY);
    char buf[16];
    snprintf(buf, sizeof(buf), "[A]%-8s", a ? a : "---");
    M5.Display.setCursor(4,   MBE_BTN_Y + 9); M5.Display.print(buf);
    snprintf(buf, sizeof(buf), "[B]%-8s", b ? b : "---");
    M5.Display.setCursor(108, MBE_BTN_Y + 9); M5.Display.print(buf);
    snprintf(buf, sizeof(buf), "[C]%-8s", c ? c : "---");
    M5.Display.setCursor(212, MBE_BTN_Y + 9); M5.Display.print(buf);
}

// ── RSSI signal-strength helpers ──────────────────────────────────────────────

static const char* mbe_rssiLabel(int8_t r) {
    if (r > -55) return "STRONG";
    if (r > -65) return "GOOD";
    if (r > -75) return "FAIR";
    if (r > -85) return "WEAK";
    return "POOR";
}
static uint16_t mbe_rssiColor(int8_t r) {
    if (r > -55) return MBE_GREEN;
    if (r > -65) return 0x37E0;
    if (r > -75) return MBE_YELLOW;
    if (r > -85) return MBE_ORANGE;
    return MBE_RED;
}
static int mbe_rssiBars(int8_t r) {
    if (r > -55) return 5;
    if (r > -65) return 4;
    if (r > -75) return 3;
    if (r > -85) return 2;
    return 1;
}
// Draw WiFi bars + label + dBm at (x,y); total height 22px
static void mbe_drawSignal(int x, int y, int8_t rssi) {
    int nb = mbe_rssiBars(rssi);
    uint16_t col = mbe_rssiColor(rssi);
    for (int i = 0; i < 5; i++) {
        int bh = (i+1)*4; int bx = x+i*8; int by = y+(22-bh);
        M5.Display.fillRect(bx, by, 6, bh, (i < nb) ? col : MBE_DK_GREY);
    }
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(col, MBE_BLACK);
    M5.Display.setCursor(x+46, y+8);
    M5.Display.print(mbe_rssiLabel(rssi));
    M5.Display.setTextColor(MBE_LT_GREY, MBE_BLACK);
    M5.Display.setCursor(x+46+7*6, y+8);
    M5.Display.printf("  %d dBm", (int)rssi);
}
// RSSI trend history (last 6 readings)
#define MBE_HIST 6
static int8_t  mbe_rH[MBE_HIST] = {0};
static uint8_t mbe_rI = 0;
static bool    mbe_rF = false;
static void mbe_rPush(int8_t r){mbe_rH[mbe_rI]=r;mbe_rI=(mbe_rI+1)%MBE_HIST;if(mbe_rI==0)mbe_rF=true;}
static int mbe_rTrend(){
    int c=mbe_rF?MBE_HIST:(int)mbe_rI; if(c<3)return 0;
    int8_t o=mbe_rH[(mbe_rI+MBE_HIST-c)%MBE_HIST], n=mbe_rH[(mbe_rI+MBE_HIST-1)%MBE_HIST];
    int d=(int)n-(int)o; return(d>=5)?1:(d<=-5)?-1:0;
}

// ── Distance estimate ("triangulation" proxy) ─────────────────────────────────
// A single receiver cannot truly triangulate (needs 2+ simultaneous readers
// at known positions) — but a free-space path-loss RSSI→distance estimate
// gives the operator a practical sense of range and closing/receding trend.
//   distance_m = 10 ^ ((TxPower - RSSI) / (10 * n))
static float mbe_estimateDistanceM(int8_t rssi) {
    const float txPowerAt1m = -40.0f;
    const float pathLossExp = 2.0f;
    float ratio = (txPowerAt1m - (float)rssi) / (10.0f * pathLossExp);
    return powf(10.0f, ratio);
}
static void mbe_drawRange(int x, int y, int8_t rssi) {
    float d = mbe_estimateDistanceM(rssi);
    char buf[24];
    if (d >= 1000.0f) snprintf(buf, sizeof(buf), "~%.1fkm est.", d / 1000.0f);
    else if (d >= 10.0f) snprintf(buf, sizeof(buf), "~%.0fm est.", d);
    else               snprintf(buf, sizeof(buf), "~%.1fm est.", d);
    M5.Display.setTextColor(MBE_LT_GREY, MBE_BLACK);
    M5.Display.setCursor(x, y);
    M5.Display.print(buf);
}

// Score bar across the full content width
static void mbe_scoreBar(int y, int score) {

    // max practical score before clamp display: 20
    int pct = (score > 20) ? 100 : (score * 100 / 20);
    uint16_t fillCol = (score >= 6) ? MBE_RED :
                       (score >= 3) ? MBE_ORANGE : MBE_GREEN;
    int f = (int)((long)(MBE_W - 16) * pct / 100);
    M5.Display.fillRect(8,     y, f,                 12, fillCol);
    M5.Display.fillRect(8 + f, y, (MBE_W-16) - f,   12, MBE_DK_GREY);
}

// Renders the "Last alert: X ago" line at the given y. Extracted into its
// own helper so it can be called both from the full-redraw path in
// m5basicUpdate() and from the cheap stale-only partial-update path (which
// reuses the cached mbe_lastAlertY rather than recomputing the whole
// layout). Uses fixed-width printf padding (not fillRect) so shorter new
// text fully overwrites longer old text via the two-color setTextColor
// cell-fill semantics — the same convention already used elsewhere in this
// codebase (e.g. flock-you-esp32's Runtime/SPIFFS status line).
static void mbe_drawAlertLine(int y, unsigned long lastAlertMs) {
    M5.Display.setTextSize(1);
    M5.Display.setCursor(8, y);
    if (lastAlertMs == 0 || mbe_lastDet[0] == '\0') {
        M5.Display.setTextColor(MBE_GREY, MBE_BLACK);
        M5.Display.printf("%-30s", "Last alert: --");
    } else if (lastAlertMs < 5000) {
        M5.Display.setTextColor(MBE_RED, MBE_BLACK);
        M5.Display.printf("%-30s", "Last alert: JUST NOW");
    } else {
        char el[12]; mbe_fmtMs(lastAlertMs, el, sizeof(el));
        char line[32]; snprintf(line, sizeof(line), "Last alert: %s ago", el);
        M5.Display.setTextColor(MBE_LT_GREY, MBE_BLACK);
        M5.Display.printf("%-30s", line);
    }
}

// Draws the reserved on-screen serial-mirror log strip.  Called just before
// the button bar in m5basicUpdate() — the region is MBE_LOG_H px tall,
// ending exactly at MBE_BTN_Y, so it never overlaps the button bar.
//
// force=false (the default) skips the actual redraw when the log content
// hasn't changed since the last draw (tracked via mbe_logVersion) — this is
// the fix for the root cause of the log-strip's own independent flicker:
// this function used to unconditionally fillRect(BLACK)+redraw on EVERY
// call, including the harmless ~250ms stale tick that fires continuously
// even with zero new log lines. force=true is passed from the full-redraw
// path in m5basicUpdate(), since that path has already fillRect(BLACK)'d
// this entire region and therefore must repaint it regardless of version.
static void mbe_drawLogStrip(bool force = false) {
    uint32_t ver = mbe_logVersion;
    static uint32_t mbe_logDrawnVersion = 0xFFFFFFFFu;
    if (!force && ver == mbe_logDrawnVersion) return;
    mbe_logDrawnVersion = ver;

    // Snapshot the ring buffer under the critical section, then draw from
    // the local copy — keeps the lock held only for the cheap memcpy, never
    // across the (much slower) SPI display calls below.
    char snap[MBE_LOG_LINES][MBE_LOG_LINE_LEN];
    portENTER_CRITICAL(&mbe_logMux);
    memcpy(snap, mbe_logBuf, sizeof(snap));
    portEXIT_CRITICAL(&mbe_logMux);

    int y0 = MBE_BTN_Y - MBE_LOG_H;
    M5.Display.fillRect(0, y0, MBE_W, MBE_LOG_H, MBE_BLACK);
    mbe_hline(y0, MBE_DK_GREY);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(0x4A69, MBE_BLACK);   // dim slate
    int ly = y0 + 3;
    for (int i = MBE_LOG_LINES - 1; i >= 0; i--) {
        M5.Display.setCursor(2, ly);
        M5.Display.print(snap[i]);
        ly += 7;
    }
}


// ── Public API ────────────────────────────────────────────────────────────────

// Called once in setup()
static void m5basicInit() {
    auto cfg = M5.config();
    cfg.internal_imu = false;
    cfg.internal_rtc = false;
    M5.begin(cfg);

    M5.Speaker.setVolume(200);

    // Core2 For AWS: 3 startup pulses to confirm vibration motor, and
    // configure the touchscreen "virtual button" strip so M5Unified maps
    // taps in the bottom MBE_BTN_H px into BtnA/BtnB/BtnC — without this call
    // M5Unified's internal touch-button height defaults to 0 and touches in
    // the [A][B][C] bar never register as button presses at all.
#if defined(USE_M5CORE2_AWS)
    for(int i=0;i<3;i++){M5.Power.setVibration(200);delay(120);M5.Power.setVibration(0);delay(80);}
    M5.setTouchButtonHeight(MBE_BTN_H);
#endif

    M5.Display.setBrightness(mbe_brightness);
    M5.Display.fillScreen(MBE_BLACK);

    // Startup splash
    M5.Display.setTextSize(3);
    M5.Display.setTextColor(MBE_CYAN, MBE_BLACK);
    M5.Display.setCursor(50, 40);
    M5.Display.print("EYE SPY");
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(MBE_LT_GREY, MBE_BLACK);
    M5.Display.setCursor(50, 90);
    M5.Display.print("Passive surveillance detector");
    M5.Display.setCursor(50, 104);
    M5.Display.print("M5Stack Basic Core v2.7");
    M5.Display.setTextColor(MBE_GREEN, MBE_BLACK);
    M5.Display.setCursor(50, 124);
    M5.Display.print("Initialising...");

    // Phase indicator placeholders
    M5.Display.setTextColor(MBE_GREY, MBE_BLACK);
    M5.Display.setCursor(50, 148);
    M5.Display.print("BLE 9s > WiFi ~3s > Promisc 5s");

    mbe_needsRedraw = true;
}

// ── Main display update ───────────────────────────────────────────────────────
// Call from updateLED() / printStatus() after score/phase may have changed.
//
//   score         — current aggregate threat score
//   lastDet       — label of most-recently triggered detection engine (may be nullptr)
//   lastRssi      — RSSI of that detection
//   phase         — "BLE" | "WIFI" | "PROMISC" | "STARTUP"
//   lastAlertMs   — millis() elapsed since g_stickySeen (0 if no detection yet)
//   trackedCount  — number of tracked unknown BLE devices
//   totalEvents   — total detection engine fire count since boot
static void m5basicUpdate(int score, const char* lastDet, int8_t lastRssi,
                           const char* phase, unsigned long lastAlertMs,
                           int trackedCount, uint32_t totalEvents) {
    int lvl = mbe_level(score);

    // WHY THIS SPLIT EXISTS: this function used to treat the ~250ms "stale"
    // timer tick (added so "Last alert: X ago" visibly counts up) as
    // equivalent to a genuine data change, and BOTH paths did the exact same
    // fillRect(BLACK)+full-body redraw. Since "stale" is true on essentially
    // every call (it only needs ~250ms to elapse), this made the ENTIRE
    // content area — score, phase, detection info, score bar, log strip —
    // flash black roughly 4x/second continuously, even with zero detections
    // and a completely idle scan. Now only a genuine dataChanged
    // (score/detection/phase/RSSI actually differs from the last redraw)
    // pays for the full clear+redraw; a stale-only tick only refreshes the
    // one line of content that's genuinely time-based (see
    // mbe_drawAlertLine()) plus checks the log strip for new lines.
    bool dataChanged = mbe_needsRedraw
                     || (score != mbe_lastScore)
                     || (lastDet && strcmp(lastDet, mbe_lastDet) != 0)
                     || (phase   && strcmp(phase,   mbe_lastPhase) != 0)
                     || (lastRssi != mbe_lastRssi);
    bool stale = (millis() - mbe_lastDrawMs) >= 250;
    if (!dataChanged && !stale) return;

    if (!dataChanged) {
        mbe_lastDrawMs = millis();
        mbe_drawAlertLine(mbe_lastAlertY, lastAlertMs);
        mbe_drawLogStrip();   // force=false: skips unless new lines arrived
        return;
    }

    mbe_lastDrawMs = millis();
    mbe_lastScore = score;
    if (lastDet) { strncpy(mbe_lastDet,   lastDet, 31); mbe_lastDet[31]   = '\0'; }
    if (phase)   { strncpy(mbe_lastPhase, phase,   11); mbe_lastPhase[11] = '\0'; }
    mbe_lastRssi = lastRssi;
    mbe_needsRedraw = false;

    // ── Header ────────────────────────────────────────────────────────────────
    char hdrL[32], hdrR[24];
    snprintf(hdrL, sizeof(hdrL), "EYE SPY  SCORE:%d", score);
    snprintf(hdrR, sizeof(hdrR), "%s", mbe_levelLabel(lvl));
    mbe_header(hdrL, hdrR, mbe_levelBg(lvl), MBE_WHITE);

    // ── Content area ──────────────────────────────────────────────────────────
    M5.Display.fillRect(0, MBE_HDR_H, MBE_W, MBE_BTN_Y - MBE_HDR_H, MBE_BLACK);

    // Top padding and inter-line gaps below are slightly tighter than the
    // original layout (6->4 top pad, 40->38 after the big score digit,
    // 22->20 after the detection name, 13->12 after the range estimate,
    // 12->11 after Total events, and three hline gaps 5->4px) to free ~11px
    // of vertical room for the enlarged log strip (MBE_LOG_H, grown from 24
    // to 33px — see mbe_drawLogStrip()) without the content area colliding
    // with it. Worst case (detection shown) verified by hand to leave a
    // ~6px gap between the score bar's bottom edge and the log strip's top
    // edge.
    int y = MBE_HDR_H + 4;

    // Large score number
    M5.Display.setTextSize(4);
    M5.Display.setTextColor(mbe_levelFg(lvl), MBE_BLACK);
    M5.Display.setCursor(8, y);
    char scoreBuf[8];
    snprintf(scoreBuf, sizeof(scoreBuf), "%d", score);
    M5.Display.print(scoreBuf);

    // Status label beside score
    M5.Display.setTextSize(2);
    int scoreW = (int)strlen(scoreBuf) * 24 + 8;
    M5.Display.setCursor(scoreW + 8, y + 8);
    M5.Display.setTextColor(mbe_levelFg(lvl), MBE_BLACK);
    M5.Display.print(mbe_levelLabel(lvl));
    y += 38;

    // Phase + scan indicator
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(MBE_LT_GREY, MBE_BLACK);
    M5.Display.setCursor(8, y);
    M5.Display.printf("Phase: %-8s  Tracked: %d", phase ? phase : "?", trackedCount);
    y += 13;

    mbe_hline(y); y += 4;

    // Last detection
    if (mbe_lastDet[0]) {
        M5.Display.setTextSize(2);
        M5.Display.setTextColor(MBE_YELLOW, MBE_BLACK);
        M5.Display.setCursor(8, y);
        // Truncate to fit 2x text (each char 12px wide, 320px → ~26 chars)
        char det[26]; strncpy(det, mbe_lastDet, 25); det[25] = '\0';
        M5.Display.print(det);
        y += 20;

        // Signal strength bars + trend arrow
        mbe_rPush(lastRssi);
        mbe_drawSignal(8, y+3, lastRssi);
        {
            int tr=mbe_rTrend();
            const char* ta=(tr>0)?"\xe2\x86\x91":(tr<0)?"\xe2\x86\x93":"\xe2\x86\x92";
            const char* tl=(tr>0)?"APPROACHING":(tr<0)?"RECEDING":"STABLE";
            uint16_t tc=(tr>0)?MBE_RED:(tr<0)?MBE_GREEN:MBE_GREY;
            M5.Display.setTextColor(tc, MBE_BLACK);
            M5.Display.setCursor(196, y+11);
            M5.Display.printf("%s %s", ta, tl);
        }
        y += 24;
        // Estimated range ("triangulation" proxy) — free-space path-loss estimate
        mbe_drawRange(8, y, lastRssi);
        y += 12;

    } else {
        M5.Display.setTextSize(1);
        M5.Display.setTextColor(MBE_GREY, MBE_BLACK);
        M5.Display.setCursor(8, y);
        M5.Display.print("No detections yet");
        y += 13;
    }

    mbe_hline(y); y += 4;

    // Time since last alert — the ONLY genuinely time-based piece of content
    // (it ticks even when score/lastDet/phase/lastRssi are all unchanged),
    // so its render logic lives in a shared helper (mbe_drawAlertLine()) and
    // its y position is cached in mbe_lastAlertY: the stale-only tick path
    // above redraws just this one line instead of paying for the full
    // fillRect(BLACK)+redraw of the whole content area on every ~250ms tick.
    mbe_lastAlertY = y;
    mbe_drawAlertLine(y, lastAlertMs);
    y += 13;

    // Total events
    M5.Display.setTextColor(MBE_GREY, MBE_BLACK);
    M5.Display.setCursor(8, y);
    M5.Display.printf("Total events: %lu  |  Score decay: 60s", (unsigned long)totalEvents);
    y += 11;

    // Score bar
    mbe_hline(y); y += 4;
    mbe_scoreBar(y, score);
    y += 14;

    // Serial-mirror log strip. force=true: this whole region was just
    // fillRect(BLACK)'d above, so it must be repainted regardless of
    // mbe_logVersion.
    mbe_drawLogStrip(true);

    // Button bar
    mbe_btnBar("RESET", "BRIGHT", "SCAN");

    // Core2 For AWS: vibration alert — non-blocking. Triggers the pattern;
    // m5basicVibrationTick() (called every loop() iteration) steps it using
    // millis() timing instead of delay(), so this never blocks button
    // polling, screen redraws, or BLE/WiFi scan phase transitions.
#if defined(USE_M5CORE2_AWS)
    {
        static int mbe_prevScore = 0;
        if (score >= 6 && mbe_prevScore < 6) {
            mbe_vibPattern = 1; mbe_vibStep = 0; mbe_vibOn = false; mbe_vibNextMs = millis();
        } else if (score >= 3 && mbe_prevScore < 3) {
            mbe_vibPattern = 2; mbe_vibStep = 0; mbe_vibOn = false; mbe_vibNextMs = millis();
        }
        mbe_prevScore = score;
    }
#endif
}

// ── Button tick ───────────────────────────────────────────────────────────────
// Returns: 0=none  1=A(reset score)  2=B(brightness)  3=C(force scan)
static int m5basicButtonTick() {
    M5.update();
    if (M5.BtnA.wasPressed()) {
        mbe_needsRedraw = true;
        return 1;
    }
    if (M5.BtnB.wasPressed()) {
        mbe_brightness = (mbe_brightness < 80)  ? 160 :
                         (mbe_brightness < 200) ? 255 : 40;
        M5.Display.setBrightness(mbe_brightness);
        return 2;
    }
    if (M5.BtnC.wasPressed()) {
        mbe_needsRedraw = true;
        return 3;
    }
    return 0;
}

#if defined(USE_M5CORE2_AWS)
// ── Vibration tick ────────────────────────────────────────────────────────────
// Call every loop() iteration (Core2 only). Steps the vibration pattern set
// by m5basicUpdate() using millis()-based timing instead of delay(), so the
// rest of loop() (buttons, screen, BLE/WiFi) never blocks.
static void m5basicVibrationTick() {
    if (mbe_vibPattern == 0) return;
    unsigned long now = millis();
    if ((long)(now - mbe_vibNextMs) < 0) return;

    const int totalPulses = (mbe_vibPattern == 1) ? 2 : 1;
    const uint8_t vibLevel = (mbe_vibPattern == 1) ? 255 : 200;
    const unsigned long onMs  = (mbe_vibPattern == 1) ? 500 : 400;
    const unsigned long offMs = 150;

    if (!mbe_vibOn) {
        if (mbe_vibStep >= totalPulses) { mbe_vibPattern = 0; M5.Power.setVibration(0); return; }
        M5.Power.setVibration(vibLevel);
        mbe_vibOn = true;
        mbe_vibNextMs = now + onMs;
    } else {
        M5.Power.setVibration(0);
        mbe_vibOn = false;
        mbe_vibStep++;
        mbe_vibNextMs = now + offMs;
    }
}
#endif

// ── Audio helpers ─────────────────────────────────────────────────────────────
static inline void m5basicBeep(uint32_t hz, uint32_t ms) {
    M5.Speaker.tone(hz, ms);
}
static inline void m5basicBeepStop() {
    M5.Speaker.stop();
}

#endif // USE_M5BASIC
