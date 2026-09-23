// c5_display.h — LILYGO T-Dongle C5 display + RGB LED helpers
// ST7735S 80x160 TFT via Adafruit_ST7735  (SPI1)
// WS2812B RGB LED on GPIO11 via Adafruit_NeoPixel
//
// Pin reference (LILYGO T-Dongle C5 schematic):
//   TFT SCLK=5  MOSI=6  CS=4  DC=2  RST=3  BL=1
//   RGB LED=11  BOOT BTN=9
#pragma once
#if defined(USE_C5_DISPLAY) && USE_C5_DISPLAY

#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <Adafruit_NeoPixel.h>
#include <cmath>
#include "alert_hold.h"   // shared critical-alert hold state machine

// ── Hardware pins ─────────────────────────────────────────────────────────────
#define C5_TFT_SCLK  5
#define C5_TFT_MOSI  6
#define C5_TFT_CS    4
#define C5_TFT_DC    2
#define C5_TFT_RST   3
#define C5_TFT_BL    1
#define C5_RGB_PIN   11   // WS2812B single LED
#define C5_BTN_PIN   9

// ── Objects (defined in header — one translation unit only) ───────────────────
static Adafruit_ST7735   c5Tft(C5_TFT_CS, C5_TFT_DC, C5_TFT_MOSI, C5_TFT_SCLK, C5_TFT_RST);
static Adafruit_NeoPixel c5Led(1, C5_RGB_PIN, NEO_GRB + NEO_KHZ800);

// ── RGB LED helpers ───────────────────────────────────────────────────────────
static inline void c5LedSet(uint8_t r, uint8_t g, uint8_t b) {
    c5Led.setPixelColor(0, c5Led.Color(r, g, b));
    c5Led.show();
}
static inline void c5LedIdle()    { c5LedSet(  0,  60,   0); }   // dim green
static inline void c5LedCaution() { c5LedSet(180,  60,   0); }   // amber
static inline void c5LedAlert()   { c5LedSet(220,   0,   0); }   // red
static inline void c5LedOff()     { c5LedSet(  0,   0,   0); }

static inline void c5LedStartup() {
    for (int i = 0; i < 3; i++) {
        c5LedSet(0, 0, 80); delay(120);
        c5LedSet(0, 0,  0); delay(80);
    }
    c5LedIdle();
}

// ── Distance estimate ("triangulation" proxy) ─────────────────────────────────
// Free-space path-loss RSSI→distance estimate. NOT true triangulation (which
// requires 2+ simultaneous readers at known positions) — a practical proxy
// for "roughly how far away is this device" from a single handheld unit.
static float c5EstimateDistanceM(int8_t rssi) {
    const float txPowerAt1m = -40.0f;
    const float pathLossExp = 2.0f;
    float ratio = (txPowerAt1m - (float)rssi) / (10.0f * pathLossExp);
    return powf(10.0f, ratio);
}
static void c5DrawRange(int x, int y, int8_t rssi, uint16_t col) {
    float d = c5EstimateDistanceM(rssi);
    char buf[20];
    if (d >= 1000.0f) snprintf(buf, sizeof(buf), "~%.1fkm est.", d / 1000.0f);
    else if (d >= 10.0f) snprintf(buf, sizeof(buf), "~%.0fm est.", d);
    else               snprintf(buf, sizeof(buf), "~%.1fm est.", d);
    c5Tft.setTextColor(col);
    c5Tft.setCursor(x, y);
    c5Tft.print(buf);
}

// ── Display init ─────────────────────────────────────────────────────────────
static void c5DisplayInit() {
    pinMode(C5_TFT_BL, OUTPUT);
    digitalWrite(C5_TFT_BL, HIGH);                 // backlight on

    c5Tft.initR(INITR_MINI160x80_PLUGIN);           // 80x160 mini TFT
    c5Tft.setRotation(1);                           // USB-C connector on right
    c5Tft.fillScreen(ST77XX_BLACK);

    // Splash screen
    c5Tft.setTextSize(1);
    c5Tft.setTextColor(ST77XX_CYAN);
    c5Tft.setCursor(4, 4);
    c5Tft.print("T-Dongle C5  ready");
    c5Tft.setTextColor(0x07FF);                     // light cyan
    c5Tft.setCursor(4, 18);
    c5Tft.print("Scanning...");

    c5Led.begin();
    c5Led.setBrightness(90);
    c5LedStartup();
}

// ── Scanning status (called from heartbeat / channel hop) ─────────────────────
static void c5DisplayScanning(uint8_t ch, int detCount) {
    c5Tft.fillScreen(ST77XX_BLACK);
    c5Tft.setTextSize(1);
    c5Tft.setTextColor(ST77XX_CYAN);
    c5Tft.setCursor(4, 4);   c5Tft.print("T-Dongle C5");
    c5Tft.setTextColor(ST77XX_GREEN);
    c5Tft.setCursor(4, 18);  c5Tft.print("Scanning...");
    c5Tft.setTextColor(ST77XX_WHITE);
    c5Tft.setCursor(4, 30);
    c5Tft.printf("Ch: %-3u  Det: %d", (unsigned)ch, detCount);
    c5LedIdle();
}

// ── Detection alert (flock-you-esp32 variant) ─────────────────────────────────
// detType  = short label for detection method ("OUI", "SSID", "BLE", etc.)
// mac      = "xx:xx:xx:xx:xx:xx"
// confidence = 0-100
// rssi       = e.g. -72
// ch         = channel number
static void c5DisplayDetection(const char* detType, const char* mac,
                                uint8_t confidence, int8_t rssi, uint8_t ch) {
    // Background colour by confidence level
    uint16_t bg = ST77XX_BLACK;
    if (confidence >= 60)      bg = 0x8000;   // dark red
    else if (confidence >= 30) bg = 0x8400;   // dark orange

    c5Tft.fillScreen(bg);
    c5Tft.setTextSize(1);

    // Header
    c5Tft.setTextColor(ST77XX_CYAN);
    c5Tft.setCursor(4, 4);
    c5Tft.print("! FLOCK DETECT !");

    // Detection type — larger text
    c5Tft.setTextSize(2);
    c5Tft.setTextColor(ST77XX_YELLOW);
    c5Tft.setCursor(4, 18);
    c5Tft.print(detType);

    // MAC (last 3 bytes) + RSSI
    c5Tft.setTextSize(1);
    c5Tft.setTextColor(ST77XX_WHITE);
    c5Tft.setCursor(4, 38);
    // show last 8 chars of mac (xx:xx:xx)
    size_t mlen = strlen(mac);
    const char* shortMac = (mlen >= 8) ? mac + mlen - 8 : mac;
    c5Tft.printf("MAC: ...%s", shortMac);

    c5Tft.setCursor(4, 50);
    c5Tft.printf("RSSI:%d  Ch:%-2u", (int)rssi, (unsigned)ch);

    // Confidence bar
    c5Tft.setCursor(4, 62);
    c5Tft.setTextColor(confidence >= 60 ? ST77XX_RED : ST77XX_YELLOW);
    c5Tft.printf("Conf: %u%%", (unsigned)confidence);

    // Estimated range ("triangulation" proxy)
    c5DrawRange(4, 72, rssi, 0xC618 /* light grey */);

    // LED
    if (confidence >= 60)      c5LedAlert();
    else if (confidence >= 30) c5LedCaution();
    else                        c5LedIdle();
}

// ── Eye-spy variant: score-based display ─────────────────────────────────────
// score     = current aggregate score
// lastDet   = short label of most-recent detection type (may be nullptr)
// phase     = "BLE" | "WIFI" | "PROMISC"
// ── Frozen CRITICAL panel (see alert_hold.h) ──────────────────────────────────
// Same content as the other boards: severity + detection type + SOURCE ADDRESS,
// all solid and held for EA_HOLD_MS so the address can be written down. This
// panel is only 80 px wide, so the 17-character address is split at the
// "aa:bb:cc:dd:ee:ff" colon boundary across two 8-character lines; one line
// would not fit at a legible size.
static AlertHoldState c5Hold;
static bool           c5HoldInited = false;

static void c5DrawHeldAlert(const char* det, const char* mac, int8_t rssi,
                            unsigned long secondsLeft, bool full) {
    if (!full) {
        c5Tft.setTextSize(1);
        c5Tft.setTextColor(0x8410);
        c5Tft.fillRect(4, 128, 76, 10, ST77XX_BLACK);
        c5Tft.setCursor(4, 128);
        c5Tft.printf("held %2lus", (unsigned long)secondsLeft);
        return;
    }

    c5Tft.fillScreen(0x8000);   // dark red
    c5Tft.setTextSize(1);
    c5Tft.setTextColor(ST77XX_CYAN);
    c5Tft.setCursor(4, 4);  c5Tft.print("EYE SPY");
    c5Tft.setTextColor(ST77XX_RED);
    c5Tft.setCursor(4, 16); c5Tft.print("!! ALERT !!");

    c5Tft.setTextColor(0x8410);
    c5Tft.setCursor(4, 32); c5Tft.print("DETECT");
    c5Tft.setTextColor(ST77XX_RED);
    c5Tft.setCursor(4, 42);
    { char d13[14]; strncpy(d13, (det && det[0]) ? det : "?", 13); d13[13] = '\0';
      c5Tft.print(d13); }

    c5Tft.setTextColor(0x8410);
    c5Tft.setCursor(4, 58); c5Tft.print("SOURCE MAC");
    if (mac && mac[0]) {
        c5Tft.setTextColor(ST77XX_RED);
        c5Tft.setCursor(4, 70); c5Tft.printf("%.8s", mac);   // "aa:bb:cc"
        c5Tft.setCursor(4, 82); c5Tft.print(mac + 9);        // "dd:ee:ff"
    } else {
        c5Tft.setTextColor(0x8410);
        c5Tft.setCursor(4, 70); c5Tft.print("-- no addr --");
    }

    c5Tft.setTextColor(0x8410);
    c5Tft.setCursor(4, 100); c5Tft.printf("RSSI %d", (int)rssi);
    c5Tft.setCursor(4, 128); c5Tft.printf("held %2lus", (unsigned long)secondsLeft);
}

static void c5DisplayScore(int score, const char* lastDet, const char* phase,
                           int8_t rssi = -100, const char* mac = nullptr) {
    // ── Critical-alert HOLD (see alert_hold.h) ───────────────────────────────
    // This panel is redrawn from scratch every tick, so a hold here simply means
    // drawing the held content instead of the live one.
    int lvl = (score >= 6) ? 2 : (score >= 3) ? 1 : 0;
    if (!c5HoldInited) { alertHoldInit(&c5Hold); c5HoldInited = true; }
    unsigned long   holdSecLeft = 0;
    AlertHoldAction holdAct = alertHoldStep(&c5Hold, lvl, lastDet, mac,
                                            rssi, millis(), &holdSecLeft);
    if (holdAct == ALERT_HOLD_DRAW || holdAct == ALERT_HOLD_TICK ||
        holdAct == ALERT_HOLD_ACTIVE) {
        // Only the countdown needs repainting on a TICK; a full redraw of a
        // 160-pixel panel every second is pure flicker for no information gain.
        c5DrawHeldAlert(c5Hold.det, c5Hold.mac, c5Hold.rssi, holdSecLeft,
                        /*full=*/holdAct != ALERT_HOLD_TICK);
        if (holdAct == ALERT_HOLD_DRAW) c5LedAlert();
        return;
    }

    uint16_t bg = ST77XX_BLACK;
    if (score >= 6)      bg = 0x8000;   // dark red
    else if (score >= 3) bg = 0x8400;   // dark amber

    c5Tft.fillScreen(bg);
    c5Tft.setTextSize(1);

    // Header
    c5Tft.setTextColor(ST77XX_CYAN);
    c5Tft.setCursor(4, 4);
    c5Tft.print("EYE SPY");

    // Score — big
    c5Tft.setTextSize(3);
    uint16_t scoreCol = (score >= 6) ? ST77XX_RED :
                        (score >= 3) ? ST77XX_YELLOW : ST77XX_GREEN;
    c5Tft.setTextColor(scoreCol);
    c5Tft.setCursor(4, 20);
    c5Tft.printf("%d", score);

    // Status label
    c5Tft.setTextSize(1);
    c5Tft.setTextColor(ST77XX_WHITE);
    c5Tft.setCursor(30, 28);
    const char* lbl = (score >= 6) ? "ALERT" : (score >= 3) ? "CAUTION" : "CLEAR";
    c5Tft.print(lbl);

    // Last detection
    if (lastDet && lastDet[0]) {
        c5Tft.setTextColor(ST77XX_YELLOW);
        c5Tft.setCursor(4, 50);
        c5Tft.print(lastDet);
    }

    // ── SOURCE ADDRESS — directly under the detection type, in the solid
    // severity colour, so "how bad" and "which device" read together and the
    // address can be written down. This panel is only 80 px wide, so the
    // "aa:bb:cc:dd:ee:ff" address is split at the colon boundary across two
    // 8-character lines; one line cannot fit at a legible size.
    c5Tft.setTextColor(0x8410);   // grey label
    c5Tft.setCursor(4, 62);
    c5Tft.print("MAC");
    if (mac && mac[0]) {
        c5Tft.setTextColor(scoreCol);
        c5Tft.setCursor(4, 72);  c5Tft.printf("%.8s", mac);   // "aa:bb:cc"
        c5Tft.setCursor(4, 82);  c5Tft.print(mac + 9);        // "dd:ee:ff"
    } else {
        // Deliberately explicit: an SSID-only match carries no address, and
        // showing a stale one from a previous detection would misidentify a
        // device — the exact confusion this line exists to prevent.
        c5Tft.setTextColor(0x8410);
        c5Tft.setCursor(4, 72);  c5Tft.print("-- no addr --");
    }

    // Phase
    c5Tft.setTextColor(0x8410);
    c5Tft.setCursor(4, 96);
    c5Tft.printf("Phase: %s", phase ? phase : "?");

    // Estimated range ("triangulation" proxy) — only when we have a real RSSI
    if (rssi > -100) c5DrawRange(4, 108, rssi, 0x8410 /* grey */);

    // LED
    if (score >= 6)      c5LedAlert();
    else if (score >= 3) c5LedCaution();
    else                  c5LedIdle();
}

#endif // USE_C5_DISPLAY
