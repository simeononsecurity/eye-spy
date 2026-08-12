// Unit tests for OUI byte-table matching (ouiMatch()).
// Runs on the host with: pio test -e native
// No hardware or ESP32 toolchain required.
//
// Mirrors flock-you-esp32's test/test_ble_matching/test_ble_matching.cpp,
// adapted to es_detect.h's actual API: OUIs are stored as raw 3-byte arrays
// (uint8_t tbl[][3]) rather than "xx:xx:xx" strings, and are matched with
// ouiMatch(const uint8_t* bssid, const uint8_t tbl[][3], size_t cnt).

#include <unity.h>
#include "../../src/es_detect.h"

// Builds a 6-byte MAC from a 3-byte OUI prefix + an arbitrary suffix.
static void macFromOui(const uint8_t oui[3], uint8_t out[6],
                        uint8_t s0 = 0x00, uint8_t s1 = 0x00, uint8_t s2 = 0x00) {
    out[0] = oui[0]; out[1] = oui[1]; out[2] = oui[2];
    out[3] = s0;     out[4] = s1;     out[5] = s2;
}

// ── FLOCK_OUIS (high-confidence Flock Safety) ────────────────────────────────

void test_flock_oui_known(void) {
    const uint8_t oui1[3] = {0x70, 0xc9, 0x4e};
    const uint8_t oui2[3] = {0x82, 0x6b, 0xf2};  // DeFlockJoplin (LAA bit set)
    const uint8_t oui3[3] = {0xb4, 0x1e, 0x52};  // Flock direct IEEE
    uint8_t mac[6];

    macFromOui(oui1, mac); TEST_ASSERT_TRUE(ouiMatch(mac, FLOCK_OUIS, NUM_FLOCK_OUIS));
    macFromOui(oui2, mac); TEST_ASSERT_TRUE(ouiMatch(mac, FLOCK_OUIS, NUM_FLOCK_OUIS));
    macFromOui(oui3, mac); TEST_ASSERT_TRUE(ouiMatch(mac, FLOCK_OUIS, NUM_FLOCK_OUIS));
}

void test_flock_oui_all_prefixes(void) {
    uint8_t mac[6];
    for (size_t i = 0; i < NUM_FLOCK_OUIS; i++) {
        macFromOui(FLOCK_OUIS[i], mac, 0xAA, 0xBB, 0xCC);
        TEST_ASSERT_TRUE(ouiMatch(mac, FLOCK_OUIS, NUM_FLOCK_OUIS));
    }
}

void test_flock_oui_no_match(void) {
    uint8_t mac[6] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    TEST_ASSERT_FALSE(ouiMatch(mac, FLOCK_OUIS, NUM_FLOCK_OUIS));
}

// ── FLOCK_MFR_OUIS (contract-manufacturer, Liteon/USI) ───────────────────────

void test_flock_mfr_oui_known(void) {
    const uint8_t liteon[3] = {0xf4, 0x6a, 0xdd};
    const uint8_t usi[3]    = {0xe0, 0x0a, 0xf6};
    uint8_t mac[6];

    macFromOui(liteon, mac); TEST_ASSERT_TRUE(ouiMatch(mac, FLOCK_MFR_OUIS, NUM_FLOCK_MFR_OUIS));
    macFromOui(usi, mac);    TEST_ASSERT_TRUE(ouiMatch(mac, FLOCK_MFR_OUIS, NUM_FLOCK_MFR_OUIS));
}

void test_flock_mfr_oui_all_prefixes(void) {
    uint8_t mac[6];
    for (size_t i = 0; i < NUM_FLOCK_MFR_OUIS; i++) {
        macFromOui(FLOCK_MFR_OUIS[i], mac, 0x11, 0x22, 0x33);
        TEST_ASSERT_TRUE(ouiMatch(mac, FLOCK_MFR_OUIS, NUM_FLOCK_MFR_OUIS));
    }
}

void test_flock_mfr_oui_not_in_flock_table(void) {
    // Contract-mfr OUIs must NOT match the high-confidence FLOCK_OUIS table.
    uint8_t mac[6];
    for (size_t i = 0; i < NUM_FLOCK_MFR_OUIS; i++) {
        macFromOui(FLOCK_MFR_OUIS[i], mac);
        TEST_ASSERT_FALSE(ouiMatch(mac, FLOCK_OUIS, NUM_FLOCK_OUIS));
    }
}

// ── SOUNDTHINKING_OUIS ────────────────────────────────────────────────────────

void test_soundthinking_oui_known(void) {
    uint8_t mac[6] = {0xd4, 0x11, 0xd6, 0xaa, 0xbb, 0xcc};
    TEST_ASSERT_TRUE(ouiMatch(mac, SOUNDTHINKING_OUIS, NUM_SOUNDTHINKING_OUIS));
}

void test_soundthinking_oui_not_in_other_tables(void) {
    uint8_t mac[6] = {0xd4, 0x11, 0xd6, 0x00, 0x00, 0x00};
    TEST_ASSERT_FALSE(ouiMatch(mac, FLOCK_OUIS, NUM_FLOCK_OUIS));
    TEST_ASSERT_FALSE(ouiMatch(mac, FLOCK_MFR_OUIS, NUM_FLOCK_MFR_OUIS));
    TEST_ASSERT_FALSE(ouiMatch(mac, ALPR_OUIS, NUM_ALPR_OUIS));
    TEST_ASSERT_FALSE(ouiMatch(mac, CAM_OUIS, NUM_CAM_OUIS));
}

// ── ALPR_OUIS (Motorola / Vigilant) ──────────────────────────────────────────

void test_alpr_oui_known(void) {
    uint8_t mac[6] = {0x00, 0x0e, 0x58, 0x01, 0x02, 0x03};
    TEST_ASSERT_TRUE(ouiMatch(mac, ALPR_OUIS, NUM_ALPR_OUIS));
}

void test_alpr_oui_not_in_other_tables(void) {
    uint8_t mac[6] = {0x00, 0x0e, 0x58, 0x00, 0x00, 0x00};
    TEST_ASSERT_FALSE(ouiMatch(mac, FLOCK_OUIS, NUM_FLOCK_OUIS));
    TEST_ASSERT_FALSE(ouiMatch(mac, FLOCK_MFR_OUIS, NUM_FLOCK_MFR_OUIS));
    TEST_ASSERT_FALSE(ouiMatch(mac, SOUNDTHINKING_OUIS, NUM_SOUNDTHINKING_OUIS));
    TEST_ASSERT_FALSE(ouiMatch(mac, CAM_OUIS, NUM_CAM_OUIS));
}

// ── CAM_OUIS (general camera-vendor, caution-level) ──────────────────────────

void test_cam_oui_known_spotcheck(void) {
    const uint8_t axis[3]      = {0x00, 0x40, 0x8c};
    const uint8_t hikvision[3] = {0x4c, 0xbd, 0x8f};
    const uint8_t dahua[3]     = {0x90, 0x02, 0xa9};
    const uint8_t ring_nest[3] = {0xf0, 0x27, 0x65};
    const uint8_t ubiquiti[3]  = {0xa8, 0x9f, 0xba};
    uint8_t mac[6];

    macFromOui(axis, mac);      TEST_ASSERT_TRUE(ouiMatch(mac, CAM_OUIS, NUM_CAM_OUIS));
    macFromOui(hikvision, mac); TEST_ASSERT_TRUE(ouiMatch(mac, CAM_OUIS, NUM_CAM_OUIS));
    macFromOui(dahua, mac);     TEST_ASSERT_TRUE(ouiMatch(mac, CAM_OUIS, NUM_CAM_OUIS));
    macFromOui(ring_nest, mac); TEST_ASSERT_TRUE(ouiMatch(mac, CAM_OUIS, NUM_CAM_OUIS));
    macFromOui(ubiquiti, mac);  TEST_ASSERT_TRUE(ouiMatch(mac, CAM_OUIS, NUM_CAM_OUIS));
}

void test_cam_oui_all_prefixes(void) {
    uint8_t mac[6];
    for (size_t i = 0; i < NUM_CAM_OUIS; i++) {
        macFromOui(CAM_OUIS[i], mac, 0x44, 0x55, 0x66);
        TEST_ASSERT_TRUE(ouiMatch(mac, CAM_OUIS, NUM_CAM_OUIS));
    }
}

void test_cam_oui_not_in_flock_table(void) {
    uint8_t mac[6];
    for (size_t i = 0; i < NUM_CAM_OUIS; i++) {
        macFromOui(CAM_OUIS[i], mac);
        TEST_ASSERT_FALSE(ouiMatch(mac, FLOCK_OUIS, NUM_FLOCK_OUIS));
    }
}

// ── Cross-table isolation ─────────────────────────────────────────────────────
// No single OUI prefix should appear in more than one of the five tables —
// a duplicate would mean two different confidence weights could fire for the
// exact same hardware, corrupting the score.

void test_all_tables_mutually_exclusive(void) {
    uint8_t mac[6];

    for (size_t i = 0; i < NUM_FLOCK_OUIS; i++) {
        macFromOui(FLOCK_OUIS[i], mac);
        TEST_ASSERT_FALSE(ouiMatch(mac, FLOCK_MFR_OUIS, NUM_FLOCK_MFR_OUIS));
        TEST_ASSERT_FALSE(ouiMatch(mac, SOUNDTHINKING_OUIS, NUM_SOUNDTHINKING_OUIS));
        TEST_ASSERT_FALSE(ouiMatch(mac, ALPR_OUIS, NUM_ALPR_OUIS));
        TEST_ASSERT_FALSE(ouiMatch(mac, CAM_OUIS, NUM_CAM_OUIS));
    }
    for (size_t i = 0; i < NUM_FLOCK_MFR_OUIS; i++) {
        macFromOui(FLOCK_MFR_OUIS[i], mac);
        TEST_ASSERT_FALSE(ouiMatch(mac, SOUNDTHINKING_OUIS, NUM_SOUNDTHINKING_OUIS));
        TEST_ASSERT_FALSE(ouiMatch(mac, ALPR_OUIS, NUM_ALPR_OUIS));
        TEST_ASSERT_FALSE(ouiMatch(mac, CAM_OUIS, NUM_CAM_OUIS));
    }
    for (size_t i = 0; i < NUM_CAM_OUIS; i++) {
        macFromOui(CAM_OUIS[i], mac);
        TEST_ASSERT_FALSE(ouiMatch(mac, SOUNDTHINKING_OUIS, NUM_SOUNDTHINKING_OUIS));
        TEST_ASSERT_FALSE(ouiMatch(mac, ALPR_OUIS, NUM_ALPR_OUIS));
    }
}

// ── Table size sanity ─────────────────────────────────────────────────────────
// Pins down the expected entry counts so a future accidental deletion/edit of
// a table is caught immediately, the same role FY_OUI_HIGH_COUNT-style
// assertions play in flock-you-esp32's test suite.

void test_oui_table_counts(void) {
    TEST_ASSERT_EQUAL_UINT(35u, (unsigned)NUM_FLOCK_OUIS);
    TEST_ASSERT_EQUAL_UINT(6u,  (unsigned)NUM_FLOCK_MFR_OUIS);
    TEST_ASSERT_EQUAL_UINT(1u,  (unsigned)NUM_SOUNDTHINKING_OUIS);
    TEST_ASSERT_EQUAL_UINT(1u,  (unsigned)NUM_ALPR_OUIS);
    TEST_ASSERT_EQUAL_UINT(31u, (unsigned)NUM_CAM_OUIS);
}

// ─────────────────────────────────────────────────────────────────────────────

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_flock_oui_known);
    RUN_TEST(test_flock_oui_all_prefixes);
    RUN_TEST(test_flock_oui_no_match);

    RUN_TEST(test_flock_mfr_oui_known);
    RUN_TEST(test_flock_mfr_oui_all_prefixes);
    RUN_TEST(test_flock_mfr_oui_not_in_flock_table);

    RUN_TEST(test_soundthinking_oui_known);
    RUN_TEST(test_soundthinking_oui_not_in_other_tables);

    RUN_TEST(test_alpr_oui_known);
    RUN_TEST(test_alpr_oui_not_in_other_tables);

    RUN_TEST(test_cam_oui_known_spotcheck);
    RUN_TEST(test_cam_oui_all_prefixes);
    RUN_TEST(test_cam_oui_not_in_flock_table);

    RUN_TEST(test_all_tables_mutually_exclusive);
    RUN_TEST(test_oui_table_counts);

    return UNITY_END();
}
