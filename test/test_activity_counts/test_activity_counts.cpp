// Unit tests for the decaying alert/caution tally (activity_counts.h).
// Runs on the host with: pio test -e native
//
// This tally replaced an on-screen "Total events" counter that only ever grew,
// which customers read as "530 alerts" and drew entirely the wrong conclusion
// from. The properties that make the replacement better are exactly the ones
// worth pinning down here:
//   - it counts EPISODES (a rise to a level), so a device parked at ALERT is
//     one alert and not thousands;
//   - it DECAYS on its own, so it describes the recent past, not all of history.
//
// Both are driven through `now`, so an hour of behaviour is tested in
// microseconds and with no hardware.

#include <unity.h>
#include "../../src/activity_counts.h"

// ── counting episodes ─────────────────────────────────────────────────────────

void test_starts_empty(void) {
    ActivityCounts c; activityCountsInit(&c, 1000);
    TEST_ASSERT_EQUAL_UINT8(0, c.alerts);
    TEST_ASSERT_EQUAL_UINT8(0, c.cautions);
    TEST_ASSERT_EQUAL_UINT32(1000, c.lastDecayMs);
}

void test_clear_level_counts_nothing(void) {
    ActivityCounts c; activityCountsInit(&c, 0);
    activityCountsNote(&c, 0, 0);
    TEST_ASSERT_EQUAL_UINT8(0, c.alerts);
    TEST_ASSERT_EQUAL_UINT8(0, c.cautions);
}

void test_alert_and_caution_are_counted_separately(void) {
    ActivityCounts c; activityCountsInit(&c, 0);
    activityCountsNote(&c, 2, 0);      // alert
    activityCountsNote(&c, 1, 0);      // caution
    activityCountsNote(&c, 1, 0);      // caution
    TEST_ASSERT_EQUAL_UINT8(1, c.alerts);
    TEST_ASSERT_EQUAL_UINT8(2, c.cautions);
}

// A jump straight to ALERT must not also count a caution: the caution level was
// never reached, and counting it would overstate how much had happened.
void test_clear_to_alert_counts_one_alert_no_caution(void) {
    ActivityCounts c; activityCountsInit(&c, 0);
    activityCountsNote(&c, 2, 0);
    TEST_ASSERT_EQUAL_UINT8(1, c.alerts);
    TEST_ASSERT_EQUAL_UINT8(0, c.cautions);
}

// The counter caps, so the on-screen text width is constant and a runaway value
// cannot wrap the layout mid-alert.
void test_counts_cap(void) {
    ActivityCounts c; activityCountsInit(&c, 0);
    for (int i = 0; i < 500; i++) {
        activityCountsNote(&c, 2, 0);
        activityCountsNote(&c, 1, 0);
    }
    TEST_ASSERT_EQUAL_UINT8(ACTIVITY_COUNT_MAX, c.alerts);
    TEST_ASSERT_EQUAL_UINT8(ACTIVITY_COUNT_MAX, c.cautions);
}

// ── decay ─────────────────────────────────────────────────────────────────────

void test_no_decay_before_the_interval(void) {
    ActivityCounts c; activityCountsInit(&c, 0);
    activityCountsNote(&c, 2, 0);
    activityCountsDecay(&c, ACTIVITY_DECAY_MS - 1);
    TEST_ASSERT_EQUAL_UINT8(1, c.alerts);
}

void test_decays_one_per_interval(void) {
    ActivityCounts c; activityCountsInit(&c, 0);
    for (int i = 0; i < 5; i++) activityCountsNote(&c, 2, 0);
    activityCountsNote(&c, 1, 0);

    activityCountsDecay(&c, ACTIVITY_DECAY_MS * 1);
    TEST_ASSERT_EQUAL_UINT8(4, c.alerts);
    TEST_ASSERT_EQUAL_UINT8(0, c.cautions);

    activityCountsDecay(&c, ACTIVITY_DECAY_MS * 3);
    TEST_ASSERT_EQUAL_UINT8(2, c.alerts);
}

// The whole point of the change: a quiet spell winds the tally down to zero by
// itself, instead of holding a lifetime total forever.
void test_decays_to_zero_and_stops(void) {
    ActivityCounts c; activityCountsInit(&c, 0);
    for (int i = 0; i < 3; i++) activityCountsNote(&c, 2, 0);
    activityCountsDecay(&c, ACTIVITY_DECAY_MS * 10);   // a long quiet spell
    TEST_ASSERT_EQUAL_UINT8(0, c.alerts);
    activityCountsDecay(&c, ACTIVITY_DECAY_MS * 20);   // must not underflow
    TEST_ASSERT_EQUAL_UINT8(0, c.alerts);
}

// Decay must not depend on new events arriving.
void test_decay_is_independent_of_new_events(void) {
    ActivityCounts c; activityCountsInit(&c, 0);
    activityCountsNote(&c, 2, 0);
    activityCountsDecay(&c, ACTIVITY_DECAY_MS);
    TEST_ASSERT_EQUAL_UINT8(0, c.alerts);
}

// A new episode during the decay window still lands, and the next decay removes
// the OLDEST point rather than ignoring the new one.
void test_new_event_between_decays_still_counts(void) {
    ActivityCounts c; activityCountsInit(&c, 0);
    activityCountsNote(&c, 2, 0);
    activityCountsNote(&c, 2, ACTIVITY_DECAY_MS / 2);
    activityCountsDecay(&c, ACTIVITY_DECAY_MS);
    TEST_ASSERT_EQUAL_UINT8(1, c.alerts);   // 2 added, 1 decayed
}


// ── robustness ────────────────────────────────────────────────────────────────

// millis() wraps after ~49 days. Signed elapsed math must keep the decay
// ticking across the wrap rather than stalling or firing wildly.
void test_millis_wrap_keeps_decaying(void) {
    const unsigned long nearWrap = 0xFFFFFF00UL;
    ActivityCounts c; activityCountsInit(&c, nearWrap);
    activityCountsNote(&c, 2, nearWrap);
    activityCountsDecay(&c, nearWrap + 300UL);          // wrapped past zero
    TEST_ASSERT_EQUAL_UINT8(1, c.alerts);               // not yet an interval
    activityCountsDecay(&c, nearWrap + ACTIVITY_DECAY_MS + 400UL);
    TEST_ASSERT_EQUAL_UINT8(0, c.alerts);               // one interval passed
}

// A long stall with many missed intervals must not loop unboundedly: the
// catch-up is capped and then resynced.
void test_long_stall_is_bounded(void) {
    ActivityCounts c; activityCountsInit(&c, 0);
    activityCountsNote(&c, 2, 0);
    activityCountsDecay(&c, ACTIVITY_DECAY_MS * 100000UL);   // pathological gap
    TEST_ASSERT_EQUAL_UINT8(0, c.alerts);
    // Resynced, so a following call does not immediately decay again.
    TEST_ASSERT_NOT_EQUAL(0, c.lastDecayMs);
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_starts_empty);
    RUN_TEST(test_clear_level_counts_nothing);
    RUN_TEST(test_alert_and_caution_are_counted_separately);
    RUN_TEST(test_clear_to_alert_counts_one_alert_no_caution);
    RUN_TEST(test_counts_cap);
    RUN_TEST(test_no_decay_before_the_interval);
    RUN_TEST(test_decays_one_per_interval);
    RUN_TEST(test_decays_to_zero_and_stops);
    RUN_TEST(test_decay_is_independent_of_new_events);
    RUN_TEST(test_new_event_between_decays_still_counts);
    RUN_TEST(test_millis_wrap_keeps_decaying);
    RUN_TEST(test_long_stall_is_bounded);

    return UNITY_END();
}

