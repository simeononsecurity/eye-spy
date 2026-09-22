// Unit tests for the BLE tracker "following" gate (AirTag / SmartTag / Tile).
// Runs on the host with: pio test -e native
// No hardware or ESP32 toolchain required.
//
// The gate exists so a tracker that merely passes by cannot contribute toward a
// SCORE_ALERT — only ~30 minutes of sustained presence does. Because
// trackerFollowUpdate() takes `now` as a parameter, these tests drive it through
// simulated hours of sightings instantly and deterministically: no millis(), no
// waiting, no hardware.
//
// Timestamps start at 1000 rather than 0, because firstSeen==0 is the "never
// seen" sentinel and a window begun at exactly 0 would collide with it (a
// sub-millisecond edge at boot on the real device, irrelevant here).

#include <unity.h>
#include "../../src/es_detect.h"

#define MIN_MS 60000UL

// ── baseline state ────────────────────────────────────────────────────────────

void test_fresh_state_is_not_following(void) {
    TrackerFollowState f; trackerFollowReset(f);
    TEST_ASSERT_FALSE(f.following);
    TEST_ASSERT_EQUAL_UINT16(0, f.hits);
    TEST_ASSERT_EQUAL_UINT32(0, f.firstSeen);
    TEST_ASSERT_EQUAL_UINT32(0, trackerFollowMinutes(f, 1000));
}

// One glimpse — the common case this gate exists to suppress — must stay at zero
// contribution no matter how strong the signal was.
void test_single_sighting_is_not_following(void) {
    TrackerFollowState f; trackerFollowReset(f);
    TEST_ASSERT_FALSE(trackerFollowUpdate(f, 1000));
    TEST_ASSERT_EQUAL_UINT16(1, f.hits);
    TEST_ASSERT_FALSE(f.following);
}

// ── the 30-minute requirement ─────────────────────────────────────────────────

void test_29_minutes_is_not_yet_following(void) {
    TrackerFollowState f; trackerFollowReset(f);
    const uint32_t t0 = 1000;
    trackerFollowUpdate(f, t0);
    for (int m = 1; m <= 29; m++) {
        TEST_ASSERT_FALSE_MESSAGE(
            trackerFollowUpdate(f, t0 + (uint32_t)m * MIN_MS),
            "must not be 'following' before the 30-minute window elapses");
    }
    TEST_ASSERT_FALSE(f.following);
}

void test_30_minutes_of_continuous_presence_is_following(void) {
    TrackerFollowState f; trackerFollowReset(f);
    const uint32_t t0 = 1000;
    trackerFollowUpdate(f, t0);
    bool following = false;
    for (int m = 1; m <= 30; m++) {
        following = trackerFollowUpdate(f, t0 + (uint32_t)m * MIN_MS);
    }
    TEST_ASSERT_TRUE(following);
    TEST_ASSERT_TRUE(f.following);
    TEST_ASSERT_EQUAL_UINT32(30, trackerFollowMinutes(f, t0 + 30 * MIN_MS));
}

// Once established, "following" stays true while sightings keep arriving — the
// alert must not lapse just because the window boundary passed.
void test_following_persists_while_continuity_holds(void) {
    TrackerFollowState f; trackerFollowReset(f);
    const uint32_t t0 = 1000;
    trackerFollowUpdate(f, t0);
    for (int m = 1; m <= 30; m++) trackerFollowUpdate(f, t0 + (uint32_t)m * MIN_MS);
    TEST_ASSERT_TRUE(f.following);
    for (int m = 31; m <= 90; m++) {
        TEST_ASSERT_TRUE(trackerFollowUpdate(f, t0 + (uint32_t)m * MIN_MS));
    }
}

// ── continuity ────────────────────────────────────────────────────────────────

void test_gap_longer_than_window_restarts_the_clock(void) {
    TrackerFollowState f; trackerFollowReset(f);
    const uint32_t t0 = 1000;
    trackerFollowUpdate(f, t0);
    trackerFollowUpdate(f, t0 + 4 * MIN_MS);          // inside continuity
    TEST_ASSERT_EQUAL_UINT16(2, f.hits);

    // Disappears for 6 minutes (> the 5-minute continuity window).
    trackerFollowUpdate(f, t0 + 10 * MIN_MS);
    TEST_ASSERT_EQUAL_UINT16(1, f.hits);              // window restarted
    TEST_ASSERT_EQUAL_UINT32(t0 + 10 * MIN_MS, f.firstSeen);
    TEST_ASSERT_FALSE(f.following);
}

// The reset is strictly "more than" the window: a gap of exactly the limit
// continues the window (boundary behaviour pinned so it can't drift silently).
void test_gap_at_exactly_the_limit_keeps_the_window(void) {
    TrackerFollowState f; trackerFollowReset(f);
    const uint32_t t0 = 1000;
    trackerFollowUpdate(f, t0);
    trackerFollowUpdate(f, t0 + TRACKER_CONTINUITY_MAX_GAP_MS);
    TEST_ASSERT_EQUAL_UINT16(2, f.hits);
    TEST_ASSERT_EQUAL_UINT32(t0, f.firstSeen);
}

// A confirmed follow that leaves and comes back later must re-qualify from
// scratch rather than instantly re-alerting on the old window.
void test_leaving_after_following_clears_it(void) {
    TrackerFollowState f; trackerFollowReset(f);
    const uint32_t t0 = 1000;
    trackerFollowUpdate(f, t0);
    for (int m = 1; m <= 30; m++) trackerFollowUpdate(f, t0 + (uint32_t)m * MIN_MS);
    TEST_ASSERT_TRUE(f.following);

    TEST_ASSERT_FALSE(trackerFollowUpdate(f, t0 + 40 * MIN_MS));  // 10-min absence
    TEST_ASSERT_FALSE(f.following);
    TEST_ASSERT_EQUAL_UINT16(1, f.hits);
}

// ── the false-positive guard ──────────────────────────────────────────────────
// THE behaviour this gate exists for: sightings spread over more than half an
// hour, but never inside the continuity window, must never add up to a follow.
// Three unrelated AirTags going past at 20-minute intervals are not following
// anyone, even though the total span exceeds 30 minutes.
void test_scattered_sightings_never_follow(void) {
    TrackerFollowState f; trackerFollowReset(f);
    uint32_t t = 1000;
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_FALSE(trackerFollowUpdate(f, t));
        TEST_ASSERT_EQUAL_UINT16(1, f.hits);   // each sighting starts a new window
        t += 20 * MIN_MS;
    }
    TEST_ASSERT_FALSE(f.following);
}

// A tracker seen every 4 minutes for an hour — the same bag on a commute — is a
// genuine sustained presence and should qualify.
void test_periodic_but_continuing_presence_qualifies(void) {
    TrackerFollowState f; trackerFollowReset(f);
    const uint32_t t0 = 1000;
    bool following = false;
    for (int i = 0; i <= 15; i++) {                 // every 4 min for 60 min
        following = trackerFollowUpdate(f, t0 + (uint32_t)i * 4 * MIN_MS);
    }
    TEST_ASSERT_TRUE(following);
    TEST_ASSERT_TRUE(f.following);
}

// ── reporting ─────────────────────────────────────────────────────────────────

// The caller in es_confidence.h (CHECK_TRACKER) treats hits==1 as "a new window
// just began" and uses that to clear its once-per-follow scored flag, so that a
// tracker which leaves and later returns can alert again. That is a contract
// between the two files, so pin it here rather than only in the macro.
void test_hits_equals_one_marks_a_new_window(void) {
    TrackerFollowState f; trackerFollowReset(f);
    trackerFollowUpdate(f, 1000);
    TEST_ASSERT_EQUAL_UINT16(1, f.hits);                    // first ever sighting
    trackerFollowUpdate(f, 1000 + 4 * MIN_MS);
    TEST_ASSERT_EQUAL_UINT16(2, f.hits);                    // same window
    trackerFollowUpdate(f, 1000 + 20 * MIN_MS);             // gap > continuity
    TEST_ASSERT_EQUAL_UINT16(1, f.hits);                    // new window ⇒ re-arm
}

void test_minutes_reporting(void) {
    TrackerFollowState f; trackerFollowReset(f);
    const uint32_t t0 = 1000;
    trackerFollowUpdate(f, t0);
    TEST_ASSERT_EQUAL_UINT32(0,  trackerFollowMinutes(f, t0));
    TEST_ASSERT_EQUAL_UINT32(0,  trackerFollowMinutes(f, t0 + 59 * 1000));  // 59 s
    TEST_ASSERT_EQUAL_UINT32(1,  trackerFollowMinutes(f, t0 + MIN_MS));
    TEST_ASSERT_EQUAL_UINT32(29, trackerFollowMinutes(f, t0 + 29 * MIN_MS));
}

// ── millis() wrap ─────────────────────────────────────────────────────────────
// The window must survive the ~49-day wrap: a sighting just after the wrap is a
// few hundred milliseconds after the one just before it — not "more than five
// minutes", and not a huge negative elapsed time. This is why the state machine
// uses uint32_t rather than unsigned long (which is 64-bit on the host).
void test_millis_wrap_does_not_break_the_window(void) {
    TrackerFollowState f; trackerFollowReset(f);
    uint32_t t = 0xFFFFFF00u;                        // 256 ms before the wrap
    TEST_ASSERT_FALSE(trackerFollowUpdate(f, t));    // hits=1
    t += 240000u;                                    // crosses the wrap
    TEST_ASSERT_FALSE(trackerFollowUpdate(f, t));    // still only 'watching'
    TEST_ASSERT_EQUAL_UINT16(2, f.hits);             // ...and NOT reset
    for (int i = 0; i < 8; i++) {                    // push past 30 min total
        t += 240000u;
        trackerFollowUpdate(f, t);
    }
    TEST_ASSERT_TRUE(f.following);
    TEST_ASSERT_EQUAL_UINT16(10, f.hits);
}

// ── constants ─────────────────────────────────────────────────────────────────

// Tripwires: the behaviour above is defined in terms of these values, so
// changing one is a deliberate act that must update this test too.
void test_threshold_values(void) {
    TEST_ASSERT_EQUAL_UINT32(1800000u, (uint32_t)TRACKER_FOLLOW_MS);             // 30 min
    TEST_ASSERT_EQUAL_UINT32(300000u,  (uint32_t)TRACKER_CONTINUITY_MAX_GAP_MS); // 5 min
    TEST_ASSERT_EQUAL_UINT16(3, (uint16_t)TRACKER_FOLLOW_MIN_HITS);
    TEST_ASSERT_TRUE(TRACKER_CONTINUITY_MAX_GAP_MS < TRACKER_FOLLOW_MS);
}

// MIN_HITS is defence-in-depth rather than the binding constraint: holding
// continuity across TRACKER_FOLLOW_MS already requires at least this many
// sightings, so the *duration* test is what actually gates the alert. Pinned here
// so a future tweak to either constant cannot silently make one of them dead or
// contradictory.
void test_min_hits_is_not_the_binding_constraint(void) {
    const unsigned long sightingsNeeded =
        TRACKER_FOLLOW_MS / TRACKER_CONTINUITY_MAX_GAP_MS + 1;
    TEST_ASSERT_TRUE(sightingsNeeded >= TRACKER_FOLLOW_MIN_HITS);
}

// ─────────────────────────────────────────────────────────────────────────────

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_fresh_state_is_not_following);
    RUN_TEST(test_single_sighting_is_not_following);

    RUN_TEST(test_29_minutes_is_not_yet_following);
    RUN_TEST(test_30_minutes_of_continuous_presence_is_following);
    RUN_TEST(test_following_persists_while_continuity_holds);

    RUN_TEST(test_gap_longer_than_window_restarts_the_clock);
    RUN_TEST(test_gap_at_exactly_the_limit_keeps_the_window);
    RUN_TEST(test_leaving_after_following_clears_it);

    RUN_TEST(test_scattered_sightings_never_follow);
    RUN_TEST(test_periodic_but_continuing_presence_qualifies);

    RUN_TEST(test_minutes_reporting);
    RUN_TEST(test_hits_equals_one_marks_a_new_window);
    RUN_TEST(test_millis_wrap_does_not_break_the_window);

    RUN_TEST(test_threshold_values);
    RUN_TEST(test_min_hits_is_not_the_binding_constraint);

    return UNITY_END();
}
