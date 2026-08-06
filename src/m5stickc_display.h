// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2024 SimeonOnSecurity <https://github.com/simeononsecurity>
//
// m5stickc_display.h — M5StickC Plus SE display helpers (eye-spy)
//
// Hardware:
//   ST7789v2 135×240 TFT — M5Unified manages via AXP192 backlight
//   Passive buzzer G2     — Arduino tone()/noTone()
//   Button A: G37  Button B: G39
//
// Display layout: rotation=3 → landscape 240w × 135h
//   Row 0–15   — header "EYE SPY  SCORE:N  CLEAR/CAUTION/ALERT"
//   Row 16–117 — main content
//   Row 118–134 — button label bar
//
// Button actions (eye-spy):
//   A — reset score to 0          (returns slot 1)
//   B — force immediate scan      (returns slot 3, same as Basic Btn C)
#pragma once
#if defined(USE_M5STICKC_PLUS_SE)

#include <M5Unified.h>
#include <cstring>
#include <cstdio>

// ── RGB565 palette ────────────────────────────────────────────────────────────
static constexpr uint16_t MSCE_BLACK    = 0x0000;
static constexpr uint16_t MSCE_WHITE    = 0xFFFF;
static constexpr uint16_t MSCE_RED      = 0xF800;
static constexpr uint16_t MSCE_GREEN    = 0x07E0;
static constexpr uint16_t MSCE_YELLOW   = 0xFFE0;
static constexpr uint16_t MSCE_CYAN     = 0x07FF;
static constexpr uint16_t MSCE_ORANGE   = 0xFD20;
static constexpr uint16_t MSCE_DARK_RED = 0x8000;
static constexpr uint16_t MSCE_DARK_AMB = 0x8280;
static constexpr uint16_t MSCE_DARK_GRN = 0x0320;
static constexpr uint16_t MSCE_GREY     = 0x8410;
static constexpr uint16_t MSCE_LT_GREY  = 0xC618;
static constexpr uint16_t MSCE_DK_GREY  = 0x2104;

// ── Layout (240×135 landscape) ────────────────────────────────────────────────
static constexpr int MSCE_W     = 240;
static constexpr int MSCE_HDR_H = 16;
static constexpr int MSCE_BTN_Y = 118;
static constexpr int MSCE_BTN_H = 17;

// ── State ─────────────────────────────────────────────────────────────────────
static uint8_t msce_brightness  = 128;
static int     msce_lastScore   = -999;
static char    msce_lastDet[32] = {0};
static int8_t  msce_lastRssi    = -100;
static char    msce_lastPhase[12]={0};
static bool    msce_needsRedraw = true;

// ── Internal helpers ──────────────────────────────────────────────────────────

static void msce_fmtMs(unsigned long ms, char* buf, size_t len) {
    unsigned long s=ms/1000, m=s/60; s%=60; unsigned long h=m/60; m%=60;
    if (h>0) snprintf(buf,len,"%lu:%02lu:%02lu",h,m,s);
    else      snprintf(buf,len,"%lu:%02lu",m,s);
}
static int msce_level(int score) {
    return (score>=6)?2:(score>=3)?1:0;
}
static uint16_t msce_levelBg(int lvl) {
    return (lvl==2)?MSCE_DARK_RED:(lvl==1)?MSCE_DARK_AMB:MSCE_DARK_GRN;
}
static uint16_t msce_levelFg(int lvl) {
    return (lvl==2)?MSCE_RED:(lvl==1)?MSCE_ORANGE:MSCE_GREEN;
}
static const char* msce_levelLabel(int lvl) {
    return (lvl==2)?"ALERT":(lvl==1)?"CAUTION":"CLEAR";
}
static void msce_hline(int y, uint16_t col = MSCE_GREY) {
    M5.Display.drawFastHLine(0, y, MSCE_W, col);
}
static void msce_header(const char* left, const char* right, uint16_t bg, uint16_t fg) {
    M5.Display.fillRect(0, 0, MSCE_W, MSCE_HDR_H, bg);
    M5.Display.setTextSize(1); M5.Display.setTextColor(fg, bg);
    M5.Display.setCursor(3, 4); M5.Display.print(left);
    if (right && right[0]) {
        int rw = (int)strlen(right)*6;
        M5.Display.setCursor(MSCE_W-rw-3, 4); M5.Display.print(right);
    }
}
static void msce_btnBar(const char* a, const char* b) {
    M5.Display.fillRect(0, MSCE_BTN_Y, MSCE_W, MSCE_BTN_H, MSCE_DK_GREY);
    msce_hline(MSCE_BTN_Y, MSCE_GREY);
    M5.Display.setTextSize(1); M5.Display.setTextColor(MSCE_LT_GREY, MSCE_DK_GREY);
    char buf[14];
    snprintf(buf,sizeof(buf),"[A]%-8s",a?a:"---");
    M5.Display.setCursor(3, MSCE_BTN_Y+5); M5.Display.print(buf);
    snprintf(buf,sizeof(buf),"[B]%-8s",b?b:"---");
    M5.Display.setCursor(122, MSCE_BTN_Y+5); M5.Display.print(buf);
}
// Compact RSSI helpers for 240×135 display
static const char* msce_rssiLabel(int8_t r){
    if(r>-55)return"STRONG"; if(r>-65)return"GOOD"; if(r>-75)return"FAIR"; if(r>-85)return"WEAK"; return"POOR";
}
static uint16_t msce_rssiColor(int8_t r){
    if(r>-55)return MSCE_GREEN; if(r>-65)return 0x37E0; if(r>-75)return MSCE_YELLOW; if(r>-85)return MSCE_ORANGE; return MSCE_RED;
}
static int msce_rssiBars(int8_t r){
    if(r>-55)return 5; if(r>-65)return 4; if(r>-75)return 3; if(r>-85)return 2; return 1;
}
// Compact 5-bar WiFi signal + label (height 15px)
static void msce_drawSig(int x, int y, int8_t rssi) {
    int nb=msce_rssiBars(rssi); uint16_t col=msce_rssiColor(rssi);
    for(int i=0;i<5;i++){int bh=(i+1)*3;int bx=x+i*6;int by=y+(15-bh);M5.Display.fillRect(bx,by,5,bh,(i<nb)?col:MSCE_DK_GREY);}
    M5.Display.setTextSize(1); M5.Display.setTextColor(col,MSCE_BLACK);
    M5.Display.setCursor(x+34,y+4); M5.Display.print(msce_rssiLabel(rssi));
    M5.Display.setTextColor(MSCE_LT_GREY,MSCE_BLACK);
    M5.Display.setCursor(x+34+6*6,y+4); M5.Display.printf(" %ddBm",(int)rssi);
}
// RSSI trend (last 4 readings)
#define MSCE_HIST 4
static int8_t msce_rH[MSCE_HIST]={0}; static uint8_t msce_rI=0; static bool msce_rF=false;
static void msce_rPush(int8_t r){msce_rH[msce_rI]=r;msce_rI=(msce_rI+1)%MSCE_HIST;if(msce_rI==0)msce_rF=true;}
static int  msce_rTrend(){
    int c=msce_rF?MSCE_HIST:(int)msce_rI; if(c<2)return 0;
    int d=(int)msce_rH[(msce_rI+MSCE_HIST-1)%MSCE_HIST]-(int)msce_rH[(msce_rI+MSCE_HIST-c)%MSCE_HIST];
    return(d>=4)?1:(d<=-4)?-1:0;
}

static void msce_scoreBar(int y, int score) {
    int pct = (score>20)?100:(score*100/20);
    uint16_t fill=(score>=6)?MSCE_RED:(score>=3)?MSCE_ORANGE:MSCE_GREEN;
    int f=(int)((long)(MSCE_W-8)*pct/100);
    M5.Display.fillRect(3,     y, f,           10, fill);
    M5.Display.fillRect(3+f,   y, (MSCE_W-8)-f, 10, MSCE_DK_GREY);
}

// ── Public API ────────────────────────────────────────────────────────────────

// ── Red LED (G10, active LOW) ──────────────────────────────────────────────
static bool msce_ledInit = false;
static void msce_setLED(bool on) {
    if (!msce_ledInit) { pinMode(10, OUTPUT); msce_ledInit = true; }
    digitalWrite(10, on ? LOW : HIGH);  // active LOW
}

// Called once from setup()
static void m5stickcInit() {
    auto cfg = M5.config();
    cfg.internal_imu = false;
    cfg.internal_rtc = false;
    cfg.internal_spk = false;   // SE has passive buzzer G2, NOT NS4168 I2S — prevent GPIO2 conflict
    M5.begin(cfg);

    // Init red LED — brief blink to confirm hardware
    msce_setLED(true); delay(120); msce_setLED(false);

    M5.Display.setRotation(3);  // landscape: 240w × 135h
    M5.Display.setBrightness(msce_brightness);
    M5.Display.fillScreen(MSCE_BLACK);

    // Splash
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(MSCE_CYAN, MSCE_BLACK);
    M5.Display.setCursor(10, 36);
    M5.Display.print("EYE SPY");
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(MSCE_LT_GREY, MSCE_BLACK);
    M5.Display.setCursor(10, 64);
    M5.Display.print("StickC Plus SE  Init...");
    M5.Display.setCursor(10, 76);
    M5.Display.print("BLE 9s > WiFi > Promisc 5s");

    msce_needsRedraw = true;
}

// ── Main display update ───────────────────────────────────────────────────────
// Call from updateLED().
static void m5stickcUpdate(int score, const char* lastDet, int8_t lastRssi,
                            const char* phase, unsigned long lastAlertMs,
                            int trackedCount, uint32_t totalEvents) {
    int lvl = msce_level(score);
    bool chg = msce_needsRedraw
            || (score != msce_lastScore)
            || (lastDet && strcmp(lastDet, msce_lastDet) != 0)
            || (phase   && strcmp(phase,   msce_lastPhase) != 0)
            || (lastRssi != msce_lastRssi);
    if (!chg) return;

    msce_lastScore = score;
    if (lastDet) { strncpy(msce_lastDet,   lastDet, 31); msce_lastDet[31]   = '\0'; }
    if (phase)   { strncpy(msce_lastPhase, phase,   11); msce_lastPhase[11] = '\0'; }
    msce_lastRssi = lastRssi;
    msce_needsRedraw = false;

    // Header
    char hdrL[24], hdrR[10];
    snprintf(hdrL, sizeof(hdrL), "EYE SPY  SCORE:%d", score);
    snprintf(hdrR, sizeof(hdrR), "%s", msce_levelLabel(lvl));
    msce_header(hdrL, hdrR, msce_levelBg(lvl), MSCE_WHITE);

    M5.Display.fillRect(0, MSCE_HDR_H, MSCE_W, MSCE_BTN_Y - MSCE_HDR_H, MSCE_BLACK);

    int y = MSCE_HDR_H + 3;

    // Large score + label
    M5.Display.setTextSize(3);
    M5.Display.setTextColor(msce_levelFg(lvl), MSCE_BLACK);
    M5.Display.setCursor(3, y);
    char sb[6]; snprintf(sb, sizeof(sb), "%d", score);
    M5.Display.print(sb);
    int scoreW = (int)strlen(sb) * 18 + 3;
    M5.Display.setTextSize(2);
    M5.Display.setCursor(scoreW + 4, y + 5);
    M5.Display.print(msce_levelLabel(lvl));
    y += 28;

    M5.Display.setTextSize(1);
    M5.Display.setTextColor(MSCE_LT_GREY, MSCE_BLACK);
    M5.Display.setCursor(3, y);
    M5.Display.printf("Phase:%-6s  Trk:%d  Evts:%lu",
                      phase?phase:"?", trackedCount, (unsigned long)totalEvents);
    y += 12;

    msce_hline(y); y += 4;

    // Last detection
    if (msce_lastDet[0]) {
        M5.Display.setTextSize(1);
        M5.Display.setTextColor(MSCE_YELLOW, MSCE_BLACK);
        M5.Display.setCursor(3, y);
        char det[26]; strncpy(det, msce_lastDet, 25); det[25]='\0';
        M5.Display.print(det); y += 11;
        // Signal bars + trend arrow
        msce_rPush(lastRssi);
        msce_drawSig(3, y, lastRssi);
        {
            int tr=msce_rTrend();
            const char* ta=(tr>0)?"\xe2\x86\x91":(tr<0)?"\xe2\x86\x93":"\xe2\x86\x92";
            uint16_t tc=(tr>0)?MSCE_RED:(tr<0)?MSCE_GREEN:MSCE_GREY;
            M5.Display.setTextColor(tc,MSCE_BLACK);
            M5.Display.setCursor(3+130,y+4); M5.Display.printf("%s",(tr>0)?"APPROACHING":(tr<0)?"RECEDING":"STABLE");
            M5.Display.print(ta);
        }
        y += 17;
    } else {
        M5.Display.setTextColor(MSCE_GREY, MSCE_BLACK);
        M5.Display.setCursor(3, y);
        M5.Display.print("No detections yet"); y += 11;
    }

    // Time since last alert
    M5.Display.setTextSize(1);
    if (lastAlertMs == 0 || !msce_lastDet[0]) {
        M5.Display.setTextColor(MSCE_GREY, MSCE_BLACK);
        M5.Display.setCursor(3, y); M5.Display.print("Last alert: --");
    } else if (lastAlertMs < 5000) {
        M5.Display.setTextColor(MSCE_RED, MSCE_BLACK);
        M5.Display.setCursor(3, y); M5.Display.print("Last alert: JUST NOW");
    } else {
        char el[12]; msce_fmtMs(lastAlertMs, el, sizeof(el));
        M5.Display.setTextColor(MSCE_LT_GREY, MSCE_BLACK);
        M5.Display.setCursor(3, y); M5.Display.printf("Last alert: %s ago", el);
    }
    y += 11;

    // Score bar
    msce_hline(y); y += 4;
    msce_scoreBar(y, score);

    msce_btnBar("RESET", "SCAN");

    // Red LED G10: mirrors threat level
    //   CLEAR   → LED off
    //   CAUTION → slow single blink
    //   ALERT   → LED solid red
    if (score >= 6) {
        msce_setLED(true);   // solid red
    } else if (score >= 3) {
        // Single blink for caution (only when level just changed)
        if (lvl != msce_level(msce_lastScore)) {
            msce_setLED(true); delay(100); msce_setLED(false);
        }
    } else {
        msce_setLED(false);
    }
}

// ── Button tick ───────────────────────────────────────────────────────────────
// Returns: 0=none  1=A(reset)  3=B(scan) — slot 3 matches Basic Btn C handler
static int m5stickcButtonTick() {
    M5.update();
    if (M5.BtnA.wasPressed()) { msce_needsRedraw = true; return 1; }
    if (M5.BtnB.wasPressed()) { msce_needsRedraw = true; return 3; }
    return 0;
}

// ── Audio helpers ─────────────────────────────────────────────────────────────
static inline void m5stickcBeep(uint32_t hz, uint32_t ms) { tone(2, hz, ms); }
static inline void m5stickcBeepStop()                      { noTone(2); }

#endif // USE_M5STICKC_PLUS_SE
