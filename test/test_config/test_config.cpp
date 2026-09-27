// Unit tests for the user-configuration codec (es_config.h).
// Runs on the host with: pio test -e native
//
// The config blob is written by the web flasher (JavaScript) and read by the
// firmware (C), so these tests are the contract between two languages. Two
// things matter more than the happy path:
//
//   1. Absent or invalid config MUST fall back to defaults — that is what a plain
//      flash produces, and it is what makes shipping this low-risk. A device that
//      refused to start, or applied half a config, because of a missing blob
//      would be a worse bug than any misconfiguration.
//   2. The CRC must be the *standard* IEEE CRC-32, because the JS computes it
//      independently. A "close enough" implementation would reject every real
//      blob and the symptom would look like "my settings did nothing".

#include <unity.h>
#include <string.h>
#include "../../src/es_config.h"

static uint8_t blob[ESCFG_TOTAL_LEN];

// ── defaults are exactly today's behaviour ───────────────────────────────────

void test_defaults_match_current_behaviour(void) {
    EsConfig c; esCfgDefaults(&c);
    TEST_ASSERT_EQUAL_UINT16(ESCFG_FLAG_ALL_OUTPUTS, c.flags);
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, c.engines);
    TEST_ASSERT_EQUAL_UINT8(6, c.minScore);          // SCORE_ALERT
    TEST_ASSERT_EQUAL_INT8(-100, c.rssiFloor);
    TEST_ASSERT_EQUAL_UINT8(30, c.trackerMin);       // the following window
}

void test_accessors_fall_back_when_not_loaded(void) {
    EsConfig c; esCfgDefaults(&c);
    c.flags = 0; c.engines = 0; c.minScore = 12; c.rssiFloor = -40; c.trackerMin = 99;
    TEST_ASSERT_TRUE(esCfgOutputEnabled(&c, false, ESCFG_FLAG_CHIRP));
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, esCfgEngines(&c, false));
    TEST_ASSERT_EQUAL_UINT8(ESCFG_DEFAULT_MIN_SCORE, esCfgMinScore(&c, false));
    TEST_ASSERT_EQUAL_INT8(ESCFG_DEFAULT_RSSI_FLOOR, esCfgRssiFloor(&c, false));
    TEST_ASSERT_EQUAL_UINT8(ESCFG_DEFAULT_TRACKER_MIN, esCfgTrackerMin(&c, false));
    TEST_ASSERT_TRUE(esCfgEngineOn(&c, false, ES_ENG_AIRTAG));
}

// ── the CRC must be the standard one, or the JS cannot agree ─────────────────

void test_crc32_matches_standard_check_value(void) {
    TEST_ASSERT_EQUAL_UINT32(0xCBF43926u, esCfgCrc32((const uint8_t*)"123456789", 9));
}

void test_crc32_empty_is_zero(void) {
    TEST_ASSERT_EQUAL_UINT32(0x00000000u, esCfgCrc32((const uint8_t*)"", 0));
}

// ── round trip and layout ────────────────────────────────────────────────────

void test_round_trip(void) {
    EsConfig in; esCfgDefaults(&in);
    in.flags = ESCFG_FLAG_CHIRP;
    in.engines = 0x00000021u;
    in.minScore = 10;
    in.rssiFloor = -80;
    in.trackerMin = 45;
    esCfgEncode(&in, blob, sizeof(blob));

    EsConfig out;
    TEST_ASSERT_TRUE(esCfgDecode(blob, sizeof(blob), &out));
    TEST_ASSERT_EQUAL_UINT16(in.flags, out.flags);
    TEST_ASSERT_EQUAL_UINT32(in.engines, out.engines);
    TEST_ASSERT_EQUAL_UINT8(in.minScore, out.minScore);
    TEST_ASSERT_EQUAL_INT8(in.rssiFloor, out.rssiFloor);
    TEST_ASSERT_EQUAL_UINT8(in.trackerMin, out.trackerMin);
}

// The flasher's JS is written against these byte offsets, so they are part of the
// interface, not an implementation detail.
void test_layout_offsets(void) {
    EsConfig in; esCfgDefaults(&in);
    in.minScore = 9; in.rssiFloor = -77; in.trackerMin = 20; in.flags = ESCFG_FLAG_LED;
    esCfgEncode(&in, blob, sizeof(blob));

    uint32_t magic; memcpy(&magic, blob + 0, 4);
    uint16_t flags; memcpy(&flags, blob + 6, 2);
    TEST_ASSERT_EQUAL_HEX32(ESCFG_MAGIC, magic);
    TEST_ASSERT_EQUAL_UINT8(ESCFG_VERSION, blob[4]);
    TEST_ASSERT_EQUAL_HEX16(ESCFG_FLAG_LED, flags);
    TEST_ASSERT_EQUAL_UINT8(9, blob[12]);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(int8_t)-77, blob[13]);
    TEST_ASSERT_EQUAL_UINT8(20, blob[14]);
    TEST_ASSERT_EQUAL_UINT8(0, blob[15]);   // reserved stays zero
}

// A blob written by the SIBLING project must be ignored, not misread — the two
// tools share a host and a user could point the wrong flasher at the wrong board.
void test_other_projects_magic_is_rejected(void) {
    EsConfig in; esCfgDefaults(&in);
    esCfgEncode(&in, blob, sizeof(blob));
    blob[0] = 0x46; blob[1] = 0x59; blob[2] = 0x43; blob[3] = 0x46;  // "FYCF"
    uint32_t crc = esCfgCrc32(blob, ESCFG_CRC_OFFSET);
    memcpy(blob + ESCFG_CRC_OFFSET, &crc, 4);   // CRC valid for the wrong magic
    EsConfig out;
    TEST_ASSERT_FALSE(esCfgDecode(blob, sizeof(blob), &out));
    TEST_ASSERT_EQUAL_UINT8(ESCFG_DEFAULT_MIN_SCORE, out.minScore);
}

// ── absent / invalid config falls back to defaults ───────────────────────────

// A freshly erased partition reads as all-0xFF: what every device flashed without
// the configurator has.
void test_blank_partition_is_invalid_and_defaults(void) {
    memset(blob, 0xFF, sizeof(blob));
    EsConfig out;
    TEST_ASSERT_FALSE(esCfgDecode(blob, sizeof(blob), &out));
    TEST_ASSERT_EQUAL_UINT8(ESCFG_DEFAULT_MIN_SCORE, out.minScore);
    TEST_ASSERT_EQUAL_UINT32(ESCFG_DEFAULT_ENGINES, out.engines);
    TEST_ASSERT_EQUAL_UINT16(ESCFG_DEFAULT_FLAGS, out.flags);
    TEST_ASSERT_EQUAL_UINT8(ESCFG_DEFAULT_TRACKER_MIN, out.trackerMin);
}

void test_wrong_magic_is_rejected(void) {
    EsConfig in; esCfgDefaults(&in);
    esCfgEncode(&in, blob, sizeof(blob));
    blob[0] ^= 0xFF;
    EsConfig out;
    TEST_ASSERT_FALSE(esCfgDecode(blob, sizeof(blob), &out));
}

// Any single-byte corruption in the covered region must be caught — the
// power-loss-mid-write case the CRC exists for.
void test_corruption_anywhere_is_caught(void) {
    EsConfig in; esCfgDefaults(&in);
    in.engines = 0x0F0F0F0Fu;
    for (int i = 0; i < ESCFG_CRC_OFFSET; i++) {
        esCfgEncode(&in, blob, sizeof(blob));
        blob[i] ^= 0x01;
        EsConfig out;
        TEST_ASSERT_FALSE_MESSAGE(esCfgDecode(blob, sizeof(blob), &out),
                                  "single-byte corruption must fail the CRC");
    }
}

void test_truncated_and_null_input_are_safe(void) {
    EsConfig in; esCfgDefaults(&in);
    esCfgEncode(&in, blob, sizeof(blob));
    EsConfig out;
    TEST_ASSERT_FALSE(esCfgDecode(blob, ESCFG_TOTAL_LEN - 1, &out));
    TEST_ASSERT_FALSE(esCfgDecode(NULL, sizeof(blob), &out));
}

// A blob from a future firmware version must be ignored, not reinterpreted: we
// cannot know what changed, and half a config is worse than none.
void test_future_and_zero_versions_are_ignored(void) {
    EsConfig in; esCfgDefaults(&in);
    EsConfig out;
    uint8_t versions[2] = { 0, (uint8_t)(ESCFG_VERSION + 1) };
    for (int i = 0; i < 2; i++) {
        esCfgEncode(&in, blob, sizeof(blob));
        blob[4] = versions[i];
        uint32_t crc = esCfgCrc32(blob, ESCFG_CRC_OFFSET);
        memcpy(blob + ESCFG_CRC_OFFSET, &crc, 4);   // keep the CRC valid
        TEST_ASSERT_FALSE(esCfgDecode(blob, sizeof(blob), &out));
        TEST_ASSERT_EQUAL_UINT8(ESCFG_DEFAULT_MIN_SCORE, out.minScore);
    }
}


// ── clamping: the load-bearing part ──────────────────────────────────────────

// A minimum score below SCORE_ALERT would let a +5 signal — including the
// shared-manufacturer OUI, which proves a Flock contractor built the hardware but
// NOT that it is a camera — alert on its own. Enforced in the firmware, not just
// hidden in the UI, and enforced through the decode path so a hand-built blob
// cannot bypass it.
void test_min_score_floor_is_enforced(void) {
    EsConfig c; esCfgDefaults(&c);
    c.minScore = 1;
    esCfgClamp(&c);
    TEST_ASSERT_EQUAL_UINT8(ESCFG_MIN_MIN_SCORE, c.minScore);

    c.minScore = 0;
    esCfgClamp(&c);
    TEST_ASSERT_EQUAL_UINT8(ESCFG_MIN_MIN_SCORE, c.minScore);

    EsConfig in; esCfgDefaults(&in);
    in.minScore = 0;
    esCfgEncode(&in, blob, sizeof(blob));
    EsConfig out;
    TEST_ASSERT_TRUE(esCfgDecode(blob, sizeof(blob), &out));
    TEST_ASSERT_EQUAL_UINT8(ESCFG_MIN_MIN_SCORE, out.minScore);
}

void test_min_score_ceiling_is_enforced(void) {
    EsConfig c; esCfgDefaults(&c);
    c.minScore = 250;
    esCfgClamp(&c);
    TEST_ASSERT_EQUAL_UINT8(ESCFG_MAX_MIN_SCORE, c.minScore);
}

void test_rssi_floor_is_clamped_both_ways(void) {
    EsConfig c; esCfgDefaults(&c);
    c.rssiFloor = (int8_t)-128;
    esCfgClamp(&c);
    TEST_ASSERT_EQUAL_INT8(ESCFG_MIN_RSSI_FLOOR, c.rssiFloor);
    c.rssiFloor = (int8_t)10;     // nonsense: stronger than any real signal
    esCfgClamp(&c);
    TEST_ASSERT_EQUAL_INT8(ESCFG_MAX_RSSI_FLOOR, c.rssiFloor);
    c.rssiFloor = (int8_t)-85;
    esCfgClamp(&c);
    TEST_ASSERT_EQUAL_INT8(-85, c.rssiFloor);
}

// 0 minutes would make a tracker alert on the first sighting, which is exactly
// what the 30-minute following gate exists to prevent.
void test_tracker_minutes_are_clamped_both_ways(void) {
    EsConfig c; esCfgDefaults(&c);
    c.trackerMin = 0;
    esCfgClamp(&c);
    TEST_ASSERT_EQUAL_UINT8(ESCFG_MIN_TRACKER_MIN, c.trackerMin);
    c.trackerMin = 255;
    esCfgClamp(&c);
    TEST_ASSERT_EQUAL_UINT8(ESCFG_MAX_TRACKER_MIN, c.trackerMin);
    c.trackerMin = 60;
    esCfgClamp(&c);
    TEST_ASSERT_EQUAL_UINT8(60, c.trackerMin);
}

void test_reserved_flag_bits_are_dropped(void) {
    EsConfig c; esCfgDefaults(&c);
    c.flags = 0xFFFF;
    esCfgClamp(&c);
    TEST_ASSERT_EQUAL_UINT16(ESCFG_FLAG_ALL_OUTPUTS, c.flags);
}

// Disabling everything is legal, and must survive intact rather than being
// "helpfully" corrected to all-on.
void test_engine_mask_may_be_empty_or_partial(void) {
    EsConfig in; esCfgDefaults(&in);
    in.engines = 0;
    esCfgEncode(&in, blob, sizeof(blob));
    EsConfig out;
    TEST_ASSERT_TRUE(esCfgDecode(blob, sizeof(blob), &out));
    TEST_ASSERT_EQUAL_UINT32(0, out.engines);
    TEST_ASSERT_FALSE(esCfgEngineOn(&out, true, ES_ENG_AIRTAG));
    TEST_ASSERT_FALSE(esCfgEngineOn(&out, true, ES_ENG_FLOCK_OUI));
    // A bit BEYOND the engine range is treated as ON, so a config saved before a
    // new engine existed cannot silently disable it.
    TEST_ASSERT_TRUE(esCfgEngineOn(&out, true, ESCFG_MAX_ENGINE));
    TEST_ASSERT_TRUE(esCfgEngineOn(&out, true, 40));
}

void test_engine_bits_are_distinct_and_in_range(void) {
    TEST_ASSERT_TRUE(ES_ENG_COUNT <= ESCFG_MAX_ENGINE);
    TEST_ASSERT_EQUAL_INT(0, ES_ENG_AXON);
    TEST_ASSERT_EQUAL_INT(ES_ENG_COUNT - 1, ES_ENG_FW_DEFAULT_MAC);
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_defaults_match_current_behaviour);
    RUN_TEST(test_accessors_fall_back_when_not_loaded);
    RUN_TEST(test_crc32_matches_standard_check_value);
    RUN_TEST(test_crc32_empty_is_zero);
    RUN_TEST(test_round_trip);
    RUN_TEST(test_layout_offsets);
    RUN_TEST(test_other_projects_magic_is_rejected);
    RUN_TEST(test_blank_partition_is_invalid_and_defaults);
    RUN_TEST(test_wrong_magic_is_rejected);
    RUN_TEST(test_corruption_anywhere_is_caught);
    RUN_TEST(test_truncated_and_null_input_are_safe);
    RUN_TEST(test_future_and_zero_versions_are_ignored);
    RUN_TEST(test_min_score_floor_is_enforced);
    RUN_TEST(test_min_score_ceiling_is_enforced);
    RUN_TEST(test_rssi_floor_is_clamped_both_ways);
    RUN_TEST(test_tracker_minutes_are_clamped_both_ways);
    RUN_TEST(test_reserved_flag_bits_are_dropped);
    RUN_TEST(test_engine_mask_may_be_empty_or_partial);
    RUN_TEST(test_engine_bits_are_distinct_and_in_range);

    return UNITY_END();
}

