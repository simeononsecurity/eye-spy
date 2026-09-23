// alert_hold.h — board-agnostic "hold a critical alert on screen" state machine
//
// Shared by every display board (m5basic_display.h, m5stickc_display.h,
// c5_display.h) rather than copied into each one. A duplicated decision rule is
// exactly what this project has been bitten by before (see the "one source of
// truth per detection-pattern table" rule in .clinerules/01-clean-code.md) —
// three copies of a hold timer would drift the moment one board's timing was
// tweaked.
//
// WHY A HOLD EXISTS: a critical detection is usually transient — the score
// decays or the source moves on — so the live dashboard would replace both the
// severity and the source address before a user could read them. The hold pins
// the panel on the last critical hit for EA_HOLD_MS so the severity stays
// obvious and the address stays legible long enough to act on or write down.
//
// This header owns ONLY the decisions (when to arm, when to fully redraw, when
// the countdown changed, when to release). Drawing stays in each board's display
// header, because the panels differ in size — the C5 is 80 px wide and has to
// wrap the address across two lines, the Core2 can show it on one.
#pragma once

#include <stdint.h>
#include <string.h>

#ifndef EA_HOLD_MS
#define EA_HOLD_MS 15000UL   // how long a critical alert stays pinned on screen
#endif

typedef struct {
    unsigned long untilMs;      // 0 / already past = not holding
    int           lastLvl;      // severity at the previous tick
    int           lastSec;      // countdown value drawn last (redraw gate)
    bool          panelDrawn;   // full panel already drawn for this hold?
    char          det[32];      // what the held panel shows
    char          mac[18];
    int8_t        rssi;
} AlertHoldState;

// What the caller should do this tick. Every value except ALERT_HOLD_NONE means
// "do NOT draw the live dashboard this tick" — the panel belongs to the hold.
typedef enum {
    ALERT_HOLD_NONE = 0,   // no hold: draw the live dashboard normally
    ALERT_HOLD_DRAW,       // (re)draw the full frozen panel
    ALERT_HOLD_TICK,       // countdown changed: refresh just the countdown line
    ALERT_HOLD_ACTIVE,     // holding, nothing to redraw this tick
    ALERT_HOLD_RELEASED    // hold just expired: draw live, and a full repaint
} AlertHoldAction;

static inline void alertHoldInit(AlertHoldState* st) {
    memset(st, 0, sizeof(*st));
    st->lastLvl = -1;     // -1 so the very first critical tick arms the hold
    st->rssi    = -100;
}

// Decide what to draw. `secLeftOut` receives the whole seconds remaining while
// holding (harmless to pass nullptr).
static AlertHoldAction alertHoldStep(AlertHoldState* st, int lvl, const char* det,
                                     const char* mac, int8_t rssi,
                                     unsigned long nowMs,
                                     unsigned long* secLeftOut) {
    const char* d = (det && det[0]) ? det : "";
    const char* m = (mac && mac[0]) ? mac : "";

    // Arm on a critical hit, and re-arm whenever the critical SOURCE changes —
    // a different address, or a different engine label. That matters because a
    // second critical hit from a *different* device arriving at 14.9 s would
    // otherwise inherit a nearly-expired window and flash past unread.
    //
    // Warnings (lvl 1) deliberately do NOT hold: they are common, and pinning the
    // screen on each one would make the device unusable.
    if (lvl >= 2 &&
        (st->lastLvl < 2 || strcmp(st->det, d) != 0 || strcmp(st->mac, m) != 0)) {
        st->untilMs    = nowMs + EA_HOLD_MS;
        st->lastSec    = -1;
        st->panelDrawn = false;
        strncpy(st->det, d, sizeof(st->det) - 1); st->det[sizeof(st->det) - 1] = '\0';
        strncpy(st->mac, m, sizeof(st->mac) - 1); st->mac[sizeof(st->mac) - 1] = '\0';
    }
    st->lastLvl = lvl;
    st->rssi    = rssi;

    if (st->untilMs != 0 && (long)(st->untilMs - nowMs) > 0) {
        unsigned long secLeft = (st->untilMs - nowMs + 999UL) / 1000UL;
        if (secLeftOut) *secLeftOut = secLeft;
        if (!st->panelDrawn) {
            st->panelDrawn = true;
            st->lastSec    = (int)secLeft;
            return ALERT_HOLD_DRAW;
        }
        if ((int)secLeft != st->lastSec) {
            st->lastSec = (int)secLeft;
            return ALERT_HOLD_TICK;
        }
        return ALERT_HOLD_ACTIVE;
    }

    // Expiry must be reported exactly once, so the caller can repaint the live
    // dashboard instead of leaving the frozen panel up indefinitely.
    if (st->untilMs != 0) {
        st->untilMs    = 0;
        st->panelDrawn = false;
        return ALERT_HOLD_RELEASED;
    }
    return ALERT_HOLD_NONE;
}
