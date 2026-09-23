// Unit tests for the critical-alert screen hold (alert_hold.h).
// Runs on the host with: pio test -e native
// No hardware or ESP32 toolchain required.
//
// The hold exists because a critical detection is usually transient: the live
// dashboard would replace both the severity and the SOURCE ADDRESS — the two
// things a user has to act on or write down — before either could be read.
//
// Because alertHoldStep() takes `now` as a parameter, these tests drive a
// 15-second hold in a few microseconds: no millis(), no waiting, no hardware.
// That is the same reason trackerFollowUpdate() takes `now` (see
// test/test_tracker_follow/), and it is what makes the re-arm and expiry edges
// testable at all.

#include <unity.h>
#include <string.h>
#include "../../src/alert_hold.h"

#define MAC_A "aa:bb:cc:dd:ee:ff"
#define MAC_B "11:22:33:44:55:66"
#define DET_1 "Flock-cam-OUI"
#define DET_2 "Raven-ble"

// Any of these means the panel is still owned by the hold this tick. Which one
// you get depends only on how much of a second has elapsed (DRAW on the first
// tick, TICK when the countdown's whole-second value changes, ACTIVE otherwise),
// so tests that care about "is it still held?" assert this rather than naming a
// sub-state they would have to recompute.
static bool stillHolding(AlertHoldAction a) {
    return a == ALERT_HOLD_DRAW || a == ALERT_HOLD_TICK || a == ALERT_HOLD_ACTIVE;
}

// ── no hold ───────────────────────────────────────────────────────────────────

void test_clear_does_not_hold(void) {
    AlertHoldState st; alertHoldInit(&st);
    unsigned long s = 99;
    TEST_ASSERT_EQUAL_INT(ALERT_HOLD_NONE,
        alertHoldStep(&st, 0, nullptr, nullptr, -100, 1000, &s));
    TEST_ASSERT_EQUAL_INT(ALERT_HOLD_NONE,
        alertHoldStep(&st, 0, nullptr, nullptr, -100, 99999, &s));
    TEST_ASSERT_EQUAL_UINT32(0, st.untilMs);
}

// A CAUTION must never pin the screen: warnings are common, and holding on each
// would make the device unreadable in a populated area.
void test_caution_never_holds(void) {
    AlertHoldState st; alertHoldInit(&st);
    TEST_ASSERT_EQUAL_INT(ALERT_HOLD_NONE,
        alertHoldStep(&st, 1, DET_1, MAC_A, -70, 1000, nullptr));
    TEST_ASSERT_EQUAL_INT(ALERT_HOLD_NONE,
        alertHoldStep(&st, 1, DET_1, MAC_A, -70, 2000, nullptr));
    TEST_ASSERT_EQUAL_UINT32(0, st.untilMs);
}

// ── arming and drawing ────────────────────────────────────────────────────────

void test_critical_arms_and_draws_once(void) {
    AlertHoldState st; alertHoldInit(&st);
    unsigned long s = 0;
    TEST_ASSERT_EQUAL_INT(ALERT_HOLD_DRAW,
        alertHoldStep(&st, 2, DET_1, MAC_A, -55, 1000, &s));
    TEST_ASSERT_EQUAL_UINT32(1000 + EA_HOLD_MS, st.untilMs);
    TEST_ASSERT_EQUAL_STRING(DET_1, st.det);
    TEST_ASSERT_EQUAL_STRING(MAC_A, st.mac);
    TEST_ASSERT_EQUAL_INT(-55, st.rssi);
    TEST_ASSERT_EQUAL_UINT32(EA_HOLD_MS / 1000UL, s);

    // Same critical source, still inside the window, same whole second elapsed:
    // no redraw is owed.
    TEST_ASSERT_EQUAL_INT(ALERT_HOLD_ACTIVE,
        alertHoldStep(&st, 2, DET_1, MAC_A, -55, 1500, &s));
}

// The countdown must redraw exactly once per whole second, not on every tick —
// repainting the panel 4x/second would make the address unreadable.
void test_countdown_redraws_once_per_second(void) {
    AlertHoldState st; alertHoldInit(&st);
    unsigned long s = 0;
    TEST_ASSERT_EQUAL_INT(ALERT_HOLD_DRAW,
        alertHoldStep(&st, 2, DET_1, MAC_A, -60, 0, &s));   // 15s left
    TEST_ASSERT_EQUAL_UINT32(15, s);

    TEST_ASSERT_EQUAL_INT(ALERT_HOLD_ACTIVE,
        alertHoldStep(&st, 2, DET_1, MAC_A, -60, 100, &s));
    TEST_ASSERT_EQUAL_INT(ALERT_HOLD_TICK,
        alertHoldStep(&st, 2, DET_1, MAC_A, -60, 1001, &s));
    TEST_ASSERT_EQUAL_UINT32(14, s);
    TEST_ASSERT_EQUAL_INT(ALERT_HOLD_ACTIVE,
        alertHoldStep(&st, 2, DET_1, MAC_A, -60, 1500, &s));
    TEST_ASSERT_EQUAL_INT(ALERT_HOLD_TICK,
        alertHoldStep(&st, 2, DET_1, MAC_A, -60, 2001, &s));
    TEST_ASSERT_EQUAL_UINT32(13, s);
}

// 1 s remaining must still round UP to 1, never down to 0 while frozen — a
// countdown reading "0" next to a still-held panel looks like a stuck device.
void test_countdown_rounds_up(void) {
    AlertHoldState st; alertHoldInit(&st);
    unsigned long s = 0;
    TEST_ASSERT_EQUAL_INT(ALERT_HOLD_DRAW,
        alertHoldStep(&st, 2, DET_1, MAC_A, -60, 0, nullptr));
    TEST_ASSERT_EQUAL_INT(ALERT_HOLD_TICK,
        alertHoldStep(&st, 2, DET_1, MAC_A, -60, EA_HOLD_MS - 500, &s));
    TEST_ASSERT_EQUAL_UINT32(1, s);
}

// ── re-arming on a new source ─────────────────────────────────────────────────

// A SECOND critical device arriving at 14.9 s must get its own full window,
// otherwise its address flashes past and is never read.
void test_new_address_rearms_the_window(void) {
    AlertHoldState st; alertHoldInit(&st);
    TEST_ASSERT_EQUAL_INT(ALERT_HOLD_DRAW,
        alertHoldStep(&st, 2, DET_1, MAC_A, -60, 0, nullptr));
    unsigned long s = 0;
    TEST_ASSERT_EQUAL_INT(ALERT_HOLD_DRAW,
        alertHoldStep(&st, 2, DET_1, MAC_B, -60, 14900, &s));
    TEST_ASSERT_EQUAL_UINT32(14900 + EA_HOLD_MS, st.untilMs);
    TEST_ASSERT_EQUAL_STRING(MAC_B, st.mac);
    TEST_ASSERT_EQUAL_UINT32(15, s);
}

// A different ENGINE label is a different critical source too (e.g. a Flock OUI
// hit after a Raven UUID hit).
void test_new_detection_label_rearms_the_window(void) {
    AlertHoldState st; alertHoldInit(&st);
    TEST_ASSERT_EQUAL_INT(ALERT_HOLD_DRAW,
        alertHoldStep(&st, 2, DET_1, MAC_A, -60, 0, nullptr));
    TEST_ASSERT_EQUAL_INT(ALERT_HOLD_DRAW,
        alertHoldStep(&st, 2, DET_2, MAC_A, -60, 5000, nullptr));
    TEST_ASSERT_EQUAL_STRING(DET_2, st.det);
}

// ── leaving and re-entering the critical state ────────────────────────────────

// Dropping to caution must NOT cut the window short: the point of the hold is
// that a transient hit stays readable after the score has already decayed.
void test_score_drop_does_not_cancel_the_hold(void) {
    AlertHoldState st; alertHoldInit(&st);
    TEST_ASSERT_EQUAL_INT(ALERT_HOLD_DRAW,
        alertHoldStep(&st, 2, DET_1, MAC_A, -60, 0, nullptr));
    // Falling to CLEAR while held must not release: the panel keeps showing the
    // critical hit until the hold expires. (Which sub-state is returned depends
    // on the countdown, so assert "still held" rather than a specific one.)
    TEST_ASSERT_TRUE(stillHolding(
        alertHoldStep(&st, 0, nullptr, nullptr, -100, 5000, nullptr)));
    TEST_ASSERT_TRUE(stillHolding(
        alertHoldStep(&st, 0, nullptr, nullptr, -100, 10000, nullptr)));
    TEST_ASSERT_EQUAL_UINT32(0 + EA_HOLD_MS, st.untilMs);
}

// ...but a critical hit that has been away and comes back is a NEW event and
// gets a fresh window.
void test_reentering_critical_rearms(void) {
    AlertHoldState st; alertHoldInit(&st);
    TEST_ASSERT_EQUAL_INT(ALERT_HOLD_DRAW,
        alertHoldStep(&st, 2, DET_1, MAC_A, -60, 0, nullptr));
    TEST_ASSERT_TRUE(stillHolding(
        alertHoldStep(&st, 0, nullptr, nullptr, -100, 3000, nullptr)));
    TEST_ASSERT_EQUAL_INT(ALERT_HOLD_DRAW,
        alertHoldStep(&st, 2, DET_1, MAC_A, -60, 5000, nullptr));
    TEST_ASSERT_EQUAL_UINT32(5000 + EA_HOLD_MS, st.untilMs);
}


// ── expiry ────────────────────────────────────────────────────────────────────

// Expiry is reported exactly once, so the caller can repaint the live dashboard
// instead of leaving the frozen panel up forever.
void test_expiry_released_once_then_none(void) {
    AlertHoldState st; alertHoldInit(&st);
    TEST_ASSERT_EQUAL_INT(ALERT_HOLD_DRAW,
        alertHoldStep(&st, 2, DET_1, MAC_A, -60, 0, nullptr));
    TEST_ASSERT_EQUAL_INT(ALERT_HOLD_RELEASED,
        alertHoldStep(&st, 2, DET_1, MAC_A, -60, EA_HOLD_MS + 1, nullptr));
    TEST_ASSERT_EQUAL_UINT32(0, st.untilMs);
    TEST_ASSERT_EQUAL_INT(ALERT_HOLD_NONE,
        alertHoldStep(&st, 2, DET_1, MAC_A, -60, EA_HOLD_MS + 2, nullptr));
}

// A source that is STILL critical when a hold expires must not immediately
// re-hold: the whole point is that the hold is transient, and if the live
// dashboard already shows the same severity + address, pinning the screen again
// would freeze it indefinitely while the threat sits there. The live panel takes
// over, showing the same information.
void test_persistent_critical_same_source_does_not_rehold(void) {
    AlertHoldState st; alertHoldInit(&st);
    TEST_ASSERT_EQUAL_INT(ALERT_HOLD_DRAW,
        alertHoldStep(&st, 2, DET_1, MAC_A, -60, 0, nullptr));
    TEST_ASSERT_EQUAL_INT(ALERT_HOLD_RELEASED,
        alertHoldStep(&st, 2, DET_1, MAC_A, -60, EA_HOLD_MS + 1, nullptr));
    TEST_ASSERT_EQUAL_INT(ALERT_HOLD_NONE,
        alertHoldStep(&st, 2, DET_1, MAC_A, -60, EA_HOLD_MS + 2, nullptr));
}

// ...but a DIFFERENT critical source arriving after a release must get its own
// window, or its address would never be readable.
void test_different_source_after_release_rearms(void) {
    AlertHoldState st; alertHoldInit(&st);
    TEST_ASSERT_EQUAL_INT(ALERT_HOLD_DRAW,
        alertHoldStep(&st, 2, DET_1, MAC_A, -60, 0, nullptr));
    TEST_ASSERT_EQUAL_INT(ALERT_HOLD_RELEASED,
        alertHoldStep(&st, 2, DET_1, MAC_A, -60, EA_HOLD_MS + 1, nullptr));
    TEST_ASSERT_EQUAL_INT(ALERT_HOLD_DRAW,
        alertHoldStep(&st, 2, DET_1, MAC_B, -60, EA_HOLD_MS + 2, nullptr));
    TEST_ASSERT_EQUAL_STRING(MAC_B, st.mac);
}

// ── robustness ────────────────────────────────────────────────────────────────

// The hold compares addresses and labels with strcmp, so null/empty must be
// safe (an SSID-only detection carries no address at all).
void test_null_and_empty_source_are_safe(void) {
    AlertHoldState st; alertHoldInit(&st);
    TEST_ASSERT_EQUAL_INT(ALERT_HOLD_DRAW,
        alertHoldStep(&st, 2, nullptr, nullptr, -60, 0, nullptr));
    TEST_ASSERT_EQUAL_STRING("", st.det);
    TEST_ASSERT_EQUAL_STRING("", st.mac);
    // Two "no address" hits of the same label must not thrash the timer (and must
    // not crash on an empty string compare).
    TEST_ASSERT_EQUAL_INT(ALERT_HOLD_ACTIVE,
        alertHoldStep(&st, 2, nullptr, "", -60, 500, nullptr));
    TEST_ASSERT_TRUE(stillHolding(
        alertHoldStep(&st, 2, "", "", -60, 1000, nullptr)));
}

// A label that would overflow the fixed buffer must be truncated, not corrupt
// the state next to it.
void test_overlong_label_is_truncated(void) {
    AlertHoldState st; alertHoldInit(&st);
    const char* longDet = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    TEST_ASSERT_EQUAL_INT(ALERT_HOLD_DRAW,
        alertHoldStep(&st, 2, longDet, MAC_A, -60, 0, nullptr));
    TEST_ASSERT_EQUAL_UINT32(sizeof(st.det) - 1, strlen(st.det));
}

// millis() wraps after ~49 days; the comparison must use signed elapsed math so
// a hold spanning the wrap does not release early (or hang forever).
void test_millis_wrap_keeps_the_hold_alive(void) {
    AlertHoldState st; alertHoldInit(&st);
    const unsigned long nearWrap = 0xFFFFFF00UL;
    TEST_ASSERT_EQUAL_INT(ALERT_HOLD_DRAW,
        alertHoldStep(&st, 2, DET_1, MAC_A, -60, nearWrap, nullptr));
    // 300 ms later the clock has wrapped past zero; still inside the window.
    TEST_ASSERT_EQUAL_INT(ALERT_HOLD_ACTIVE,
        alertHoldStep(&st, 2, DET_1, MAC_A, -60, nearWrap + 300UL, nullptr));
}


void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_clear_does_not_hold);
    RUN_TEST(test_caution_never_holds);
    RUN_TEST(test_critical_arms_and_draws_once);
    RUN_TEST(test_countdown_redraws_once_per_second);
    RUN_TEST(test_countdown_rounds_up);
    RUN_TEST(test_new_address_rearms_the_window);
    RUN_TEST(test_new_detection_label_rearms_the_window);
    RUN_TEST(test_score_drop_does_not_cancel_the_hold);
    RUN_TEST(test_reentering_critical_rearms);
    RUN_TEST(test_expiry_released_once_then_none);
    RUN_TEST(test_persistent_critical_same_source_does_not_rehold);
    RUN_TEST(test_different_source_after_release_rearms);
    RUN_TEST(test_null_and_empty_source_are_safe);
    RUN_TEST(test_overlong_label_is_truncated);
    RUN_TEST(test_millis_wrap_keeps_the_hold_alive);

    return UNITY_END();
}

