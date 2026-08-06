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
// Timestamp of the last actual redraw — used to force a periodic (~1 Hz)
// repaint even when score/lastDet/phase/lastRssi are unchanged, so the
// "Last alert: X ago" clock visibly ticks instead of appearing frozen
// between phase transitions (BLE→WIFI→PROMISC, which can be many seconds apart).
static unsigned long mbe_lastDrawMs = 0;

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

// ── Public API ────────────────────────────────────────────────────────────────

// Called once in setup()
static void m5basicInit() {
    auto cfg = M5.config();
    cfg.internal_imu = false;
    cfg.internal_rtc = false;
    M5.begin(cfg);

    M5.Speaker.setVolume(200);

    // Core2 For AWS: 3 startup pulses to confirm vibration motor
#if defined(USE_M5CORE2_AWS)
    for(int i=0;i<3;i++){M5.Power.setVibration(200);delay(120);M5.Power.setVibration(0);delay(80);}
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

    // Only redraw if meaningful state changed
    bool stale = (millis() - mbe_lastDrawMs) >= 1000;
    bool changed = mbe_needsRedraw
                 || (score != mbe_lastScore)
                 || (lastDet && strcmp(lastDet, mbe_lastDet) != 0)
                 || (phase   && strcmp(phase,   mbe_lastPhase) != 0)
                 || (lastRssi != mbe_lastRssi)
                 || stale;
    if (!changed) return;

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

    int y = MBE_HDR_H + 6;

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
    y += 40;

    // Phase + scan indicator
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(MBE_LT_GREY, MBE_BLACK);
    M5.Display.setCursor(8, y);
    M5.Display.printf("Phase: %-8s  Tracked: %d", phase ? phase : "?", trackedCount);
    y += 13;

    mbe_hline(y); y += 7;

    // Last detection
    if (mbe_lastDet[0]) {
        M5.Display.setTextSize(2);
        M5.Display.setTextColor(MBE_YELLOW, MBE_BLACK);
        M5.Display.setCursor(8, y);
        // Truncate to fit 2x text (each char 12px wide, 320px → ~26 chars)
        char det[26]; strncpy(det, mbe_lastDet, 25); det[25] = '\0';
        M5.Display.print(det);
        y += 22;

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
        y += 13;

    } else {
        M5.Display.setTextSize(1);
        M5.Display.setTextColor(MBE_GREY, MBE_BLACK);
        M5.Display.setCursor(8, y);
        M5.Display.print("No detections yet");
        y += 13;
    }

    mbe_hline(y); y += 7;

    // Time since last alert
    M5.Display.setTextSize(1);
    if (lastAlertMs == 0 || mbe_lastDet[0] == '\0') {
        M5.Display.setTextColor(MBE_GREY, MBE_BLACK);
        M5.Display.setCursor(8, y);
        M5.Display.print("Last alert: --");
    } else if (lastAlertMs < 5000) {
        M5.Display.setTextColor(MBE_RED, MBE_BLACK);
        M5.Display.setCursor(8, y);
        M5.Display.print("Last alert: JUST NOW");
    } else {
        char el[12]; mbe_fmtMs(lastAlertMs, el, sizeof(el));
        M5.Display.setTextColor(MBE_LT_GREY, MBE_BLACK);
        M5.Display.setCursor(8, y);
        M5.Display.printf("Last alert: %s ago", el);
    }
    y += 13;

    // Total events
    M5.Display.setTextColor(MBE_GREY, MBE_BLACK);
    M5.Display.setCursor(8, y);
    M5.Display.printf("Total events: %lu  |  Score decay: 60s", (unsigned long)totalEvents);
    y += 16;

    // Score bar
    mbe_hline(y); y += 7;
    mbe_scoreBar(y, score);
    y += 18;

    // Cooldown note
    M5.Display.setTextColor(MBE_DK_GREY | 0x1000, MBE_BLACK); // slightly brighter dark
    M5.Display.setCursor(8, y);
    M5.Display.print("Re-score cooldown: 120s per engine");

    // Button bar
    mbe_btnBar("RESET", "BRIGHT", "SCAN");

    // Core2 For AWS: vibrate when score crosses ALERT or CAUTION threshold
#if defined(USE_M5CORE2_AWS)
    {
        static int mbe_prevScore = 0;
        if (score >= 6 && mbe_prevScore < 6) {
            // Two strong pulses = new ALERT
            M5.Power.setVibration(255); delay(500); M5.Power.setVibration(0);
            delay(150);
            M5.Power.setVibration(255); delay(500); M5.Power.setVibration(0);
        } else if (score >= 3 && mbe_prevScore < 3) {
            // One medium pulse = entering CAUTION
            M5.Power.setVibration(200); delay(400); M5.Power.setVibration(0);
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

// ── Audio helpers ─────────────────────────────────────────────────────────────
static inline void m5basicBeep(uint32_t hz, uint32_t ms) {
    M5.Speaker.tone(hz, ms);
}
static inline void m5basicBeepStop() {
    M5.Speaker.stop();
}

#endif // USE_M5BASIC
