// activity_counts.h — decaying tally of alert and caution EPISODES, for the screen
//
// Replaces the old on-screen "Total events" line, which counted every detection-
// engine fire since boot. That number only ever grew, so it was useless for
// judging the present moment and actively misleading: three customers reported
// "530 alerts" from an 8-hour session and concluded the device was broken or
// had found nothing. What a user actually wants to see is how much has been
// happening LATELY, which means the tally has to be able to fall as well as rise.
//
// Two deliberate properties:
//
//   1. It counts EPISODES, not time. A rise to a level is counted once and then
//      ignored until the level falls and rises again. A device parked at ALERT
//      for an hour is ONE alert, not 3600 — otherwise a persistent threat would
//      inflate the number exactly like the counter this replaces.
//   2. Each tally loses one per decay interval, independently of whether new
//      events arrive. So a quiet spell winds the number down to zero on its own,
//      and the display always reflects "recently", never "ever".
//
// This header holds only the arithmetic (no drawing, no Arduino/ESP-IDF), so the
// whole rise/decay lifecycle is unit-tested on the host. It takes `now` as a
// parameter for the same reason trackerFollowUpdate() and alertHoldStep() do:
// calling millis() internally would make the decay edges untestable.
#pragma once

#include <stdint.h>

// How long one point takes to fall off the tally. Deliberately the same span the
// scorer uses to consider a signal "new again" (DETECTION_RESCORE_MS = 120 s in
// es_confidence.h), so the tally and the reading fade on the same timescale
// rather than disagreeing about what "recent" means.
#ifndef ACTIVITY_DECAY_MS
#define ACTIVITY_DECAY_MS 120000UL
#endif

// Two digits is all the display has room for, and a tally larger than that
// carries no extra meaning anyway. Capping also makes the width of the on-screen
// text constant, which keeps the layout from shifting mid-alert.
#define ACTIVITY_COUNT_MAX 99

typedef struct {
    uint8_t       alerts;      // level-2 (red) episodes
    uint8_t       cautions;    // level-1 (yellow) episodes
    unsigned long lastDecayMs; // when the tallies were last decremented
} ActivityCounts;

static inline void activityCountsInit(ActivityCounts* c, unsigned long now) {
    c->alerts      = 0;
    c->cautions    = 0;
    c->lastDecayMs = now;
}

// Count the episode reached at `level` — call this on the RISING EDGE only
// (exactly where the chime fires, so the screen tally and the audible alert
// always agree). level 1 = caution, level 2 = alert, 0 = clear (no-op).
//
// A jump straight from clear to alert counts ONE alert and no caution: the
// caution level was never reached, and counting it would overstate how much had
// happened.
static inline void activityCountsNote(ActivityCounts* c, int level,
                                      unsigned long now) {
    (void)now;
    if (level >= 2) {
        if (c->alerts   < ACTIVITY_COUNT_MAX) c->alerts++;
    } else if (level == 1) {
        if (c->cautions < ACTIVITY_COUNT_MAX) c->cautions++;
    }
}

// Decay both tallies by one per elapsed interval. Safe to call every UI tick:
// returns immediately when no interval has passed. Uses signed elapsed-time
// arithmetic so the ~49-day millis() wrap neither stalls the decay nor fires it
// thousands of times at once.
static inline void activityCountsDecay(ActivityCounts* c, unsigned long now) {
    // Guard against a pathological burst (e.g. a long stall with a tiny
    // interval): after this many catch-up steps, resync instead of looping.
    // The tallies can never exceed ACTIVITY_COUNT_MAX anyway, so extra steps
    // would be no-ops — this only bounds the loop.
    const int kMaxCatchUpSteps = 8;
    int steps = 0;
    while ((long)(now - c->lastDecayMs) >= (long)ACTIVITY_DECAY_MS &&
           steps < kMaxCatchUpSteps) {
        c->lastDecayMs += ACTIVITY_DECAY_MS;
        if (c->alerts)   c->alerts--;
        if (c->cautions) c->cautions--;
        steps++;
    }
    if (steps == kMaxCatchUpSteps) c->lastDecayMs = now;   // resync after a stall
}
