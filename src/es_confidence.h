// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2024 SimeonOnSecurity <https://github.com/simeononsecurity>
//
// es_confidence.h — Eye Spy confidence-score engine
//
// WHY THIS FILE EXISTS (de-monolithic refactor, mirrors flock-you-esp32's
// fy_confidence.h): the per-engine point weights, detector state bookkeeping
// (the DECL_DETECTOR/CHECK_DET macros + the volatile flags they declare),
// score accumulation/decay, and the long-lived device-persistence tracker
// (for "same unknown MAC seen repeatedly") are one cohesive concern —
// confidence scoring — separate from the detection engines that *feed* it
// (BLE callback, WiFi scan, promiscuous sniffer, all still in main.cpp) and
// separate from the LED/display code that *consumes* g_score (updateLED(),
// also still in main.cpp). Previously all of this was interleaved directly
// in main.cpp; isolating it here means a scoring-weight tweak (e.g.
// changing PTS_FLOCK_OUI) can't accidentally touch detection-engine or
// display logic, and vice versa.
//
// Public interface expected by main.cpp:
//   Score weights:  PTS_* defines
//   Thresholds:     SCORE_ALERT, SCORE_CAUTION, SCORE_DECAY_INTERVAL,
//                    DETECTION_RESCORE_MS
//   Detector state: DECL_DETECTOR(name) macro + one instance per engine
//                    (declared here), CHECK_DET(name, pts, tag) macro
//   Score state:    g_score, g_lastDecay, g_stickySeen
//   Functions:      addScore(), tickDecay()
//   Persistence:    TrackedDev, g_tracked[], g_trackedCount, purgeTracked()
//
// NOTE ON INCLUDE ORDER: CHECK_DET(...) expands to code that calls
// mbeDetTrack() and the IF_M5BASIC_LOG(...) macro — both defined later in
// main.cpp, *before* CHECK_DET is actually invoked inside processBLE().
// This is safe because macro bodies are only type-checked at their
// expansion site (single translation unit, single-pass compile), not at
// #define time — so it doesn't matter that this header (and the CHECK_DET
// #define it contains) is included earlier in the file than
// mbeDetTrack()/IF_M5BASIC_LOG, as long as those are declared above
// processBLE() where CHECK_DET is actually expanded.

#pragma once

#include <cstdint>
#include <cstring>

// ── Timing / thresholds ──────────────────────────────────────────────────────
#define SCORE_DECAY_INTERVAL  60000UL
#define DETECTION_RESCORE_MS 120000UL
#define SCORE_ALERT            6
#define SCORE_CAUTION          3

// ── Per-engine score weights ──────────────────────────────────────────────────
#define PTS_AXON               5
#define PTS_RAYBAN             5
#define PTS_FLOCK_BLE          5
#define PTS_FLOCK_BLE_MFR      5   // BLE mfr-ID 0x09C8 (XUNTONG/Flock confirmed)
#define PTS_RAVEN_BLE          5   // Raven GATT service UUIDs
#define PTS_SKIMMER            5
#define PTS_AIRTAG             4
#define PTS_ODID_BLE           4
#define PTS_ODID_WIFI          4
#define PTS_SOUNDTHINKING      4   // SoundThinking/ShotSpotter co-deployed with Flock
#define PTS_SMARTTAG           3
#define PTS_TILE               3
#define PTS_MESHCORE           2
#define PTS_IBEACON            2
#define PTS_PERSIST            2
#define PTS_FLOCK_OUI          5
#define PTS_ALPR_OUI           5
#define PTS_FLOCK_SSID         5
#define PTS_ALPR_SSID          4
#define PTS_CAM_OUI            3
#define PTS_CAM_SSID           2
#define PTS_FLOCK_MFR_OUI      2   // Liteon/USI contract-mfr — shared hardware, low conf

// ── Detection state for each BLE engine ───────────────────────────────────────
#define DECL_DETECTOR(name) \
    static volatile bool          g_##name##Det      = false; \
    static volatile int8_t        g_##name##Rssi     = -100;  \
    static volatile unsigned long g_##name##Seen     = 0;     \
    static          unsigned long g_##name##Scored   = 0;     \
    static          uint16_t      g_##name##Count    = 0;     \
    static          unsigned long g_##name##LoggedAt = 0

DECL_DETECTOR(axon);
DECL_DETECTOR(rayban);
DECL_DETECTOR(flockBle);
DECL_DETECTOR(flockBleMfr);   // BLE mfr-ID 0x09C8 (XUNTONG)
DECL_DETECTOR(ravenBle);      // Raven GATT service UUIDs
DECL_DETECTOR(skimmer);
DECL_DETECTOR(airtag);
DECL_DETECTOR(odidBle);
DECL_DETECTOR(odidWifi);
DECL_DETECTOR(smarttag);
DECL_DETECTOR(tile);
DECL_DETECTOR(meshcore);
DECL_DETECTOR(ibeacon);
DECL_DETECTOR(persist);

// WiFi scored inline (not volatile — set/read only from loop(), never an ISR)
static unsigned long g_flockOuiScored       = 0;
static unsigned long g_flockMfrOuiScored    = 0;
static unsigned long g_soundthinkingScored  = 0;
static unsigned long g_alprOuiScored        = 0;
static unsigned long g_flockSsidScored      = 0;
static unsigned long g_alprSsidScored       = 0;
static unsigned long g_camOuiScored         = 0;
static unsigned long g_camSsidScored        = 0;

// ── Aggregate confidence score ────────────────────────────────────────────────
static int           g_score      = 0;
static unsigned long g_lastDecay  = 0;
// Last time any BLE detection flag was active in processBLE().
// While this is within DETECTION_RESCORE_MS, score decay is suppressed
// so the LED keeps alerting until the device actually disappears.
static unsigned long g_stickySeen = 0;

// ── Device persistence tracker ────────────────────────────────────────────────
#define MAX_TRACKED 50
#define PERSIST_MIN_COUNT   3
#define PERSIST_MIN_MS   300000UL  // 5 minutes
#define TRACKED_PURGE_INTERVAL_MS  60000UL
#define TRACKED_STALE_MS        1800000UL  // 30 minutes

struct TrackedDev {
    char     addr[18];
    unsigned long firstSeen;
    unsigned long lastSeen;
    uint16_t seenCount;
    bool     scored;
};
static TrackedDev g_tracked[MAX_TRACKED];
static uint8_t    g_trackedCount = 0;

// ── Scoring ────────────────────────────────────────────────────────────────────
static void addScore(int pts, unsigned long now, unsigned long* ts, const char* tag) {
    if (now - *ts < DETECTION_RESCORE_MS) return;
    *ts = now;
    g_score += pts;
    Serial.printf("[eyespy] +%d (%s)  score=%d\n", pts, tag, g_score);
}

// Fires once per still-active detector flag: logs, updates the m5basic
// on-screen log strip, and scores the hit. See "NOTE ON INCLUDE ORDER"
// above regarding mbeDetTrack()/IF_M5BASIC_LOG() forward-use.
#define CHECK_DET(name, pts, tag) \
    if (g_##name##Det) { \
        g_##name##Det = false; \
        g_##name##Count++; \
        g_stickySeen = now; \
        mbeDetTrack(tag, (int8_t)g_##name##Rssi); \
        if (g_##name##Count == 1 || now - g_##name##LoggedAt >= DETECTION_RESCORE_MS) { \
            g_##name##LoggedAt = now; \
            Serial.printf("[eyespy] " tag "  RSSI=%d  #%u\n", \
                          (int)g_##name##Rssi, (unsigned)g_##name##Count); \
            IF_M5BASIC_LOG(tag, (int)g_##name##Rssi, (unsigned)g_##name##Count); \
        } \
        addScore(pts, now, &g_##name##Scored, tag); \
    }

// ── Score decay ──────────────────────────────────────────────────────────────
static void tickDecay() {
    unsigned long now = millis();
    // Sticky alert: suppress decay while any detection type is still actively
    // being seen (within DETECTION_RESCORE_MS).  Once the device disappears,
    // g_stickySeen stops being refreshed and decay resumes after the window.
    if (now - g_stickySeen < DETECTION_RESCORE_MS) return;
    if (now - g_lastDecay >= SCORE_DECAY_INTERVAL) {
        g_lastDecay = now;
        if (g_score > 0) { g_score--; Serial.printf("[eyespy] decay  score=%d\n", g_score); }
    }
}

// ── Purge stale tracked devices ───────────────────────────────────────────────
static void purgeTracked() {
    static unsigned long lastPurge = 0;
    unsigned long now = millis();
    if (now - lastPurge < TRACKED_PURGE_INTERVAL_MS) return;
    lastPurge = now;
    uint8_t w = 0;
    for (uint8_t i = 0; i < g_trackedCount; i++) {
        if (now - g_tracked[i].lastSeen < TRACKED_STALE_MS)
            g_tracked[w++] = g_tracked[i];
    }
    g_trackedCount = w;
}
