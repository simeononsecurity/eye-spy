// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2024 SimeonOnSecurity <https://github.com/simeononsecurity>
//
// ui_task.h — Eye Spy: decoupled UI/display FreeRTOS task
//
// WHY THIS FILE EXISTS:
// Previously ALL user-facing I/O (status LED colour/blink, buzzer/speaker
// alert tones, on-screen redraws, and M5Unified button polling) was driven
// directly from updateLED(), called once per loop() iteration — the exact
// same task/call-stack that also runs the BLE scan callback, WiFi scan
// parsing, and the promiscuous-mode channel hopper. Each display function is
// internally rate-limited (~4Hz via a *_lastDrawMs staleness check) so this
// mostly *looked* fine, but the UI's actual redraw/flash cadence was still at
// the mercy of however long the current scan phase took to return control to
// loop() (WiFi.scanNetworks() wait states, the delay(30)/delay(50) calls
// during phase transitions, etc).
//
// This header moves that entire "human feedback" concern onto its own
// FreeRTOS task, pinned to the same core Arduino's loopTask normally runs on
// (Core 1) so WiFi/BT (Core 0) is left completely undisturbed. The UI task
// redraws/flashes at a smooth, constant ~20Hz poll cadence, fully
// independent of scan timing — while still faithfully acting on the latest
// scan-derived score/phase/detection data.
//
// Scanning code (BLE callback, WiFi scan parser, promiscuous sniffer,
// es_confidence.h's scoring engine) is completely untouched — it keeps
// writing into the same g_score/g_phase/g_mbeLastDet/etc. globals it always
// has. The only new thing is a small mutex-protected snapshot (uiPublish())
// that the scan side calls once per loop() iteration (from updateLED(),
// which is now just a thin publish call) to hand off the values the UI task
// needs.
//
// THREAD-SAFETY CONTRACT (read this before touching either task's code):
//  - M5Unified / Adafruit_ST7735 / Adafruit_NeoPixel objects are NOT
//    thread-safe. After this header is wired in, ONLY uiTaskFn() (running on
//    the UI task) may call display-drawing functions, M5.update(), the
//    button-tick functions, or the vibration tick. main.cpp's loop() /
//    scanning code must never call them directly again.
//  - Data crosses tasks ONLY through the UiSnapshot struct below, copied
//    in/out under a short critical section (portENTER_CRITICAL/EXIT) — no
//    lock is ever held during the actual (slow) SPI/I2C redraw itself.
//  - Button presses are surfaced back to the scanning side via a tiny
//    mutex-protected "pending action" byte (uiTakeButtonAction()) rather
//    than the UI task directly mutating scan state (g_score, g_phase, WiFi/
//    NimBLE calls) — those APIs are not guaranteed safe to call from an
//    arbitrary task, so the actual state change still happens on the
//    scanning side, in loop(), exactly as before.
//  - setLED()/audioAlert() are safe to call from the UI task: setLED() is a
//    simple NeoPixel primitive (led_neopixel.h) with no shared mutable state
//    beyond the strip object itself (which only the UI task now touches),
//    and audioAlert() only fires a non-blocking tone().
//
// Public interface expected by main.cpp:
//   uiPublish(score, lastDet, lastRssi, phase, lastAlertMs, trackedCount, totalEvents)
//                          — called once per loop() iteration (from updateLED())
//   uiTakeButtonAction()  — called once per loop() iteration; returns 0/1/3
//                            (matches the pre-existing m5basicButtonTick()/
//                            m5stickcButtonTick() action codes) and clears it
//   startUiTask()         — called once from setup(), AFTER g_startupMs has
//                            been assigned

#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// ── Snapshot published by the scanning side, consumed by the UI task ─────────
struct UiSnapshot {
    int           score;
    char          lastDet[32];
    int8_t        lastRssi;
    char          phase[12];
    unsigned long lastAlertMs;
    int           trackedCount;
    uint32_t      totalEvents;
};

static UiSnapshot   g_uiSnap = { 0, {0}, -100, {0}, 0, 0, 0 };
static portMUX_TYPE  g_uiMux = portMUX_INITIALIZER_UNLOCKED;

// Button action reported by the UI task, consumed once by loop().
// 0 = none   1 = "A" (reset score)   3 = "B"/"C" (force immediate scan)
static volatile uint8_t g_uiButtonAction = 0;
static portMUX_TYPE     g_uiBtnMux = portMUX_INITIALIZER_UNLOCKED;

// Called from the scanning side (updateLED(), every loop() iteration).
// Cheap: just a bounded strncpy/memcpy under a short critical section.
static void uiPublish(int score, const char* lastDet, int8_t lastRssi,
                       const char* phase, unsigned long lastAlertMs,
                       int trackedCount, uint32_t totalEvents) {
    portENTER_CRITICAL(&g_uiMux);
    g_uiSnap.score = score;
    if (lastDet) { strncpy(g_uiSnap.lastDet, lastDet, sizeof(g_uiSnap.lastDet) - 1);
                   g_uiSnap.lastDet[sizeof(g_uiSnap.lastDet) - 1] = '\0'; }
    else           g_uiSnap.lastDet[0] = '\0';
    g_uiSnap.lastRssi = lastRssi;
    if (phase)    { strncpy(g_uiSnap.phase, phase, sizeof(g_uiSnap.phase) - 1);
                    g_uiSnap.phase[sizeof(g_uiSnap.phase) - 1] = '\0'; }
    g_uiSnap.lastAlertMs  = lastAlertMs;
    g_uiSnap.trackedCount = trackedCount;
    g_uiSnap.totalEvents  = totalEvents;
    portEXIT_CRITICAL(&g_uiMux);
}

// Called once per loop() iteration by the scanning side to pick up (and
// clear) any button action the UI task recorded since the last check.
static uint8_t uiTakeButtonAction() {
    uint8_t a;
    portENTER_CRITICAL(&g_uiBtnMux);
    a = g_uiButtonAction;
    g_uiButtonAction = 0;
    portEXIT_CRITICAL(&g_uiBtnMux);
    return a;
}

// Called only from the UI task. If more than one action arrives between
// loop() checks (shouldn't happen at human button-press speed, but be
// defensive), the first one wins rather than being clobbered.
static void uiSetButtonAction(uint8_t a) {
    portENTER_CRITICAL(&g_uiBtnMux);
    if (g_uiButtonAction == 0) g_uiButtonAction = a;
    portEXIT_CRITICAL(&g_uiBtnMux);
}

// ── The UI task itself ────────────────────────────────────────────────────────
static TaskHandle_t g_uiTaskHandle = nullptr;

static void uiTaskFn(void* pv) {
    (void)pv;
    static int prevLevel = 0;
    const TickType_t period = pdMS_TO_TICKS(50);   // ~20Hz poll; each display
                                                     // fn still self-throttles
                                                     // its own actual redraw.
    for (;;) {
        unsigned long now = millis();

        // Startup window: mirrors updateLED()'s previous early-return — only
        // the blue confirmation pulse plays, no display/audio yet, while
        // NimBLE/WiFi are still coming up. g_startupMs is written exactly
        // once in setup() before this task starts, then never mutated again,
        // so reading it here from another task is safe without a lock.
        if (now - g_startupMs < STARTUP_DURATION_MS) {
            bool on = ((now / STARTUP_PULSE_MS) & 1) == 0;
            setLED(0, 0, on ? 40 : 0);
            vTaskDelay(period);
            continue;
        }

        UiSnapshot snap;
        portENTER_CRITICAL(&g_uiMux);
        snap = g_uiSnap;
        portEXIT_CRITICAL(&g_uiMux);

        int level = (snap.score >= SCORE_ALERT)   ? 2 :
                    (snap.score >= SCORE_CAUTION) ? 1 : 0;
        if (level > prevLevel) audioAlert(level >= 2);
        prevLevel = level;

        if (level >= 2) {
            bool on = ((now / ALERT_FLASH_HALF_MS) & 1) == 0;
            setLED(on ? 220 : 0, 0, 0);
        } else if (level == 1) {
            setLED(180, 60, 0);
        } else {
            setLED(0, 80, 0);
        }

        const char* det = snap.lastDet[0] ? snap.lastDet : nullptr;

#if defined(USE_C5_DISPLAY) && USE_C5_DISPLAY
        c5DisplayScore(snap.score, det, snap.phase, snap.lastRssi);
#endif
#if defined(USE_M5BASIC)
        m5basicUpdate(snap.score, det, snap.lastRssi, snap.phase,
                      snap.lastAlertMs, snap.trackedCount, snap.totalEvents);
        {
            int btn = m5basicButtonTick();
            if (btn == 1 || btn == 3) uiSetButtonAction((uint8_t)btn);
            // btn==2 (brightness cycle) is fully self-contained inside
            // m5basicButtonTick() already — nothing for the scan side to do.
        }
#if defined(USE_M5CORE2_AWS)
        m5basicVibrationTick();
#endif
#endif
#if defined(USE_M5STICKC_PLUS_SE)
        m5stickcUpdate(snap.score, det, snap.lastRssi, snap.phase,
                       snap.lastAlertMs, snap.trackedCount, snap.totalEvents);
        {
            int btn = m5stickcButtonTick();
            if (btn == 1 || btn == 3) uiSetButtonAction((uint8_t)btn);
        }
#endif

        vTaskDelay(period);
    }
}

// Called once from setup(), AFTER g_startupMs has been assigned. Display
// init (m5basicInit()/m5stickcInit()/c5DisplayInit()) must already have run
// on the setup()/loopTask context before this — the task only ever *draws*
// to an already-initialised display object.
static void startUiTask() {
    xTaskCreatePinnedToCore(uiTaskFn, "eyespy_ui", 4096, nullptr,
                             1, &g_uiTaskHandle, 1);
}
