// Unit tests for the on-device detection log framing (es_log.h).
// Runs on the host with: pio test -e native
//
// Why these matter more than a happy path: this log is only ever read back
// *after* something has already gone wrong — a crash, a power cut, a unit posted
// back to us. So the failures that matter are the quiet ones: a slot that reads
// back as valid but is not what was written, a ring index pointing at the wrong
// record, or a torn write silently accepted as a record. Every test below is
// either a roundtrip proof or an attempt to make the reader accept something it
// should refuse.

#include <unity.h>
#include <string.h>
#include "../../src/es_log.h"

static uint8_t slot[ESL_SLOT_SIZE];
static char    out[ESL_TEXT_MAX + 1];

// ── capacity and ring arithmetic ─────────────────────────────────────────────

void test_capacity_matches_the_partition_size(void) {
    // The real `eslog` partition is 64 KB (partitions_4mb.csv): a 16-byte header
    // followed by 128-byte slots, rounded down.
    TEST_ASSERT_EQUAL_UINT32(511, eslogCapacity(65536));
    TEST_ASSERT_EQUAL_UINT32(31,  eslogCapacity(4096));
    TEST_ASSERT_EQUAL_UINT32(1,   eslogCapacity(ESL_HDR_SIZE + ESL_SLOT_SIZE));
    // A partition no bigger than its own header is "too small", not a division by
    // zero and not one slot.
    TEST_ASSERT_EQUAL_UINT32(0,   eslogCapacity(ESL_HDR_SIZE));
    TEST_ASSERT_EQUAL_UINT32(0,   eslogCapacity(ESL_HDR_SIZE - 1));
    TEST_ASSERT_EQUAL_UINT32(0,   eslogCapacity(0));
}

void test_slot_offsets_start_after_the_header(void) {
    TEST_ASSERT_EQUAL_UINT32(ESL_HDR_SIZE, eslogSlotOff(0));
    TEST_ASSERT_EQUAL_UINT32(ESL_HDR_SIZE + ESL_SLOT_SIZE, eslogSlotOff(1));
    TEST_ASSERT_EQUAL_UINT32(ESL_HDR_SIZE + 2 * ESL_SLOT_SIZE, eslogSlotOff(2));
    // The last slot of a 64 KB partition must still fit inside it.
    uint32_t cap = eslogCapacity(65536);
    TEST_ASSERT_TRUE(eslogSlotOff(cap - 1) + ESL_SLOT_SIZE <= 65536);
}

void test_next_slot_wraps_at_capacity(void) {
    TEST_ASSERT_EQUAL_UINT32(1,   eslogNextSlot(0, 511));
    TEST_ASSERT_EQUAL_UINT32(510, eslogNextSlot(509, 511));
    TEST_ASSERT_EQUAL_UINT32(0,   eslogNextSlot(510, 511));
    TEST_ASSERT_EQUAL_UINT32(0,   eslogNextSlot(0, 0));      // no capacity, no panic
}

void test_count_and_oldest_before_and_after_wrap(void) {
    // Before the ring wraps, records live at 0..total-1 and the oldest is slot 0.
    TEST_ASSERT_EQUAL_UINT32(0,   eslogCount(0, 10));
    TEST_ASSERT_EQUAL_UINT32(4,   eslogCount(4, 10));
    TEST_ASSERT_EQUAL_UINT32(0,   eslogOldest(4, 10, 4));
    // Once wrapped, count pins to capacity and the oldest is head (the next slot
    // to be overwritten), NOT slot 0 — getting this wrong would replay the newest
    // records first and read the oldest as garbage.
    TEST_ASSERT_EQUAL_UINT32(10,  eslogCount(10, 10));
    TEST_ASSERT_EQUAL_UINT32(10,  eslogCount(15, 10));
    TEST_ASSERT_EQUAL_UINT32(5,   eslogOldest(15, 10, 5));
    TEST_ASSERT_EQUAL_UINT32(0,   eslogOldest(20, 10, 0));
}

// ── record framing ───────────────────────────────────────────────────────────

void test_record_roundtrip_preserves_text_and_uptime(void) {
    const char* text = "[eyespy] +5 (Axon-cam)  score=10";
    TEST_ASSERT_EQUAL_UINT16(strlen(text), eslogBuildSlot(slot, text, 1234u));
    uint32_t up = 0;
    TEST_ASSERT_TRUE(eslogParseSlot(slot, out, sizeof(out), &up));
    TEST_ASSERT_EQUAL_STRING(text, out);
    TEST_ASSERT_EQUAL_UINT32(1234u, up);
}

void test_a_real_detection_line_roundtrips(void) {
    // The whole point of storing text rather than a binary record: these are
    // lines `api/eyespy.py` already parses, so a dump replays as a detection.
    const char* lines[] = {
        "[eyespy] +5 (Axon-cam)  score=10",
        "[eyespy] Raven-BLE-UUID  RSSI=-71  #3",
        "[eyespy] AirTag  RSSI=-88  #12  watching (2min/30min)",
        "[eyespy] +6 (AirTag FOLLOWING 30min)  score=6",
    };
    for (unsigned i = 0; i < sizeof(lines) / sizeof(lines[0]); i++) {
        TEST_ASSERT_EQUAL_UINT16(strlen(lines[i]), eslogBuildSlot(slot, lines[i], 60u));
        TEST_ASSERT_TRUE(eslogParseSlot(slot, out, sizeof(out), NULL));
        TEST_ASSERT_EQUAL_STRING(lines[i], out);
    }
}

void test_highest_uptime_survives(void) {
    // uptime is seconds since boot; a unit left running for weeks must not wrap
    // its timestamp into a plausible-looking small number.
    TEST_ASSERT_EQUAL_UINT16(3, eslogBuildSlot(slot, "abc", 0xFFFFFFFFu));
    uint32_t up = 0;
    TEST_ASSERT_TRUE(eslogParseSlot(slot, out, sizeof(out), &up));
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, up);
}

void test_corrupted_text_is_refused(void) {
    const char* text = "[eyespy] +5 (RayBan-Meta)  score=5";
    eslogBuildSlot(slot, text, 90u);
    TEST_ASSERT_TRUE(eslogParseSlot(slot, out, sizeof(out), NULL));
    // Flip one bit in the text: the CRC must catch it, because a partially
    // written or bit-rotted record that still parsed would be worse than no
    // record at all — it would look like evidence.
    slot[ESL_SLOT_HDR + 4] ^= 0x01;
    TEST_ASSERT_FALSE(eslogParseSlot(slot, out, sizeof(out), NULL));
}

void test_wrong_magic_is_refused(void) {
    eslogBuildSlot(slot, "[eyespy] x", 1u);
    slot[0] = 0x00;                      // erased-flash byte
    TEST_ASSERT_FALSE(eslogParseSlot(slot, out, sizeof(out), NULL));
    slot[0] = ESL_MAGIC_SLOT;
    TEST_ASSERT_TRUE(eslogParseSlot(slot, out, sizeof(out), NULL));
}

void test_zero_and_oversized_lengths_are_refused(void) {
    eslogBuildSlot(slot, "[eyespy] x", 1u);
    slot[2] = 0; slot[3] = 0;            // len = 0
    TEST_ASSERT_FALSE(eslogParseSlot(slot, out, sizeof(out), NULL));
    eslogBuildSlot(slot, "[eyespy] x", 1u);
    slot[2] = (uint8_t)((ESL_TEXT_MAX + 1) & 0xFF);   // len just past the max
    slot[3] = (uint8_t)(((ESL_TEXT_MAX + 1) >> 8) & 0xFF);
    TEST_ASSERT_FALSE(eslogParseSlot(slot, out, sizeof(out), NULL));
}

void test_overlong_and_empty_text_are_refused_by_the_encoder(void) {
    char big[ESL_TEXT_MAX + 2];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    // Refused, not truncated: a cut-off line may no longer match an API pattern,
    // so storing one would corrupt a replay in a way nobody would notice.
    TEST_ASSERT_EQUAL_UINT16(0, eslogBuildSlot(slot, big, 1u));
    TEST_ASSERT_EQUAL_UINT16(0, eslogBuildSlot(slot, "", 1u));
    TEST_ASSERT_EQUAL_UINT16(0, eslogBuildSlot(NULL, "x", 1u));
    TEST_ASSERT_EQUAL_UINT16(0, eslogBuildSlot(slot, NULL, 1u));
    // Exactly at the limit is fine — that boundary is what a long tracker line
    // could reach, so it must not be off by one.
    char exact[ESL_TEXT_MAX + 1];
    memset(exact, 'y', ESL_TEXT_MAX);
    exact[ESL_TEXT_MAX] = '\0';
    TEST_ASSERT_EQUAL_UINT16(ESL_TEXT_MAX, eslogBuildSlot(slot, exact, 1u));
    TEST_ASSERT_TRUE(eslogParseSlot(slot, out, sizeof(out), NULL));
    TEST_ASSERT_EQUAL_STRING(exact, out);
}

void test_short_output_buffer_truncates_the_copy_but_stays_valid(void) {
    const char* text = "[eyespy] +5 (Axon-cam)  score=10";
    eslogBuildSlot(slot, text, 7u);
    char tiny[11];
    uint32_t up = 0;
    TEST_ASSERT_TRUE(eslogParseSlot(slot, tiny, sizeof(tiny), &up));
    TEST_ASSERT_EQUAL_UINT32(7u, up);            // uptime is unaffected by the cap
    TEST_ASSERT_EQUAL_UINT32(10, (uint32_t)strlen(tiny));
    TEST_ASSERT_EQUAL_INT(0, strncmp(tiny, text, 10));
    // A buffer with no room for even one character must be refused, not written
    // past.
    TEST_ASSERT_FALSE(eslogParseSlot(slot, tiny, 1, NULL));
}

// ── partition header ─────────────────────────────────────────────────────────

void test_header_roundtrip(void) {
    uint8_t h[ESL_HDR_SIZE];
    eslogBuildHdr(h, 137u, 4000u);
    uint32_t head = 0, total = 0;
    TEST_ASSERT_TRUE(eslogParseHdr(h, &head, &total));
    TEST_ASSERT_EQUAL_UINT32(137u,  head);
    TEST_ASSERT_EQUAL_UINT32(4000u, total);
}

void test_blank_partition_is_not_a_valid_header(void) {
    // A freshly erased (or never written) partition reads as all-0xFF. Treating
    // that as a header would mean reading a ring whose head and total are
    // 0xFFFFFFFF: the dump would claim billions of records and read garbage.
    uint8_t h[ESL_HDR_SIZE];
    memset(h, 0xFF, sizeof(h));
    TEST_ASSERT_FALSE(eslogParseHdr(h, NULL, NULL));
    // Same for an all-zero partition.
    memset(h, 0x00, sizeof(h));
    TEST_ASSERT_FALSE(eslogParseHdr(h, NULL, NULL));
    TEST_ASSERT_FALSE(eslogParseHdr(NULL, NULL, NULL));
}

void test_header_rejects_an_unknown_version(void) {
    uint8_t h[ESL_HDR_SIZE];
    eslogBuildHdr(h, 1u, 1u);
    h[4] = 0x02;                     // pretend a future format wrote this
    h[5] = 0x00;
    TEST_ASSERT_FALSE(eslogParseHdr(h, NULL, NULL));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_capacity_matches_the_partition_size);
    RUN_TEST(test_slot_offsets_start_after_the_header);
    RUN_TEST(test_next_slot_wraps_at_capacity);
    RUN_TEST(test_count_and_oldest_before_and_after_wrap);
    RUN_TEST(test_record_roundtrip_preserves_text_and_uptime);
    RUN_TEST(test_a_real_detection_line_roundtrips);
    RUN_TEST(test_highest_uptime_survives);
    RUN_TEST(test_corrupted_text_is_refused);
    RUN_TEST(test_wrong_magic_is_refused);
    RUN_TEST(test_zero_and_oversized_lengths_are_refused);
    RUN_TEST(test_overlong_and_empty_text_are_refused_by_the_encoder);
    RUN_TEST(test_short_output_buffer_truncates_the_copy_but_stays_valid);
    RUN_TEST(test_header_roundtrip);
    RUN_TEST(test_blank_partition_is_not_a_valid_header);
    RUN_TEST(test_header_rejects_an_unknown_version);
    return UNITY_END();
}
