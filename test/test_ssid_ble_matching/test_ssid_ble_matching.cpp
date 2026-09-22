// Unit tests for SSID keyword matching (ssidHas()) and case-insensitive
// substring matching (strContainsCI()), plus sanity checks on the
// nullptr-terminated pattern arrays they consume.
// Runs on the host with: pio test -e native
// No hardware or ESP32 toolchain required.
//
// Mirrors flock-you-esp32's test/test_ble_matching/test_ble_matching.cpp
// (BLE-name section) and test/test_uuid_matching/test_uuid_matching.cpp
// (UUID-count-sanity section), adapted to es_detect.h's actual API.

#include <unity.h>
#include <cstdio>
#include <cstring>
#include "../../src/es_detect.h"


// ── ssidHas() — FLOCK_SSID_KW ────────────────────────────────────────────────

void test_ssidHas_flock_keywords(void) {
    TEST_ASSERT_TRUE(ssidHas("Flock Camera net.", FLOCK_SSID_KW));
    TEST_ASSERT_TRUE(ssidHas("FlockSafety-Cam-01", FLOCK_SSID_KW));
    TEST_ASSERT_TRUE(ssidHas("FS Ext Battery 4", FLOCK_SSID_KW));
    TEST_ASSERT_TRUE(ssidHas("Penguin_Hotspot", FLOCK_SSID_KW));
    TEST_ASSERT_TRUE(ssidHas("PigVision-42", FLOCK_SSID_KW));
    TEST_ASSERT_TRUE(ssidHas("Raven Node 7", FLOCK_SSID_KW));
}

void test_ssidHas_flock_all_keywords(void) {
    // Every keyword in the table, wrapped in extra text, must still match.
    for (const char** kw = FLOCK_SSID_KW; *kw; kw++) {
        char ssid[64];
        snprintf(ssid, sizeof(ssid), "Test-%s-Suffix", *kw);
        TEST_ASSERT_TRUE_MESSAGE(ssidHas(ssid, FLOCK_SSID_KW), *kw);
    }
}

// ── ssidHas() — ALPR_SSID_KW ──────────────────────────────────────────────────

void test_ssidHas_alpr_keywords(void) {
    TEST_ASSERT_TRUE(ssidHas("ALPR-Unit-9", ALPR_SSID_KW));
    TEST_ASSERT_TRUE(ssidHas("Vigilant_Cam", ALPR_SSID_KW));
    TEST_ASSERT_TRUE(ssidHas("Motorola-LPR", ALPR_SSID_KW));
    TEST_ASSERT_TRUE(ssidHas("PlateReader3", ALPR_SSID_KW));
}

void test_ssidHas_alpr_all_keywords(void) {
    for (const char** kw = ALPR_SSID_KW; *kw; kw++) {
        char ssid[64];
        snprintf(ssid, sizeof(ssid), "Test-%s-Suffix", *kw);
        TEST_ASSERT_TRUE_MESSAGE(ssidHas(ssid, ALPR_SSID_KW), *kw);
    }
}

// ── ssidHas() — CAM_SSID_KW ───────────────────────────────────────────────────

void test_ssidHas_cam_keywords(void) {
    TEST_ASSERT_TRUE(ssidHas("MyIPCam", CAM_SSID_KW));
    TEST_ASSERT_TRUE(ssidHas("Hikvision-DVR", CAM_SSID_KW));
    TEST_ASSERT_TRUE(ssidHas("Front Doorbell", CAM_SSID_KW));
    TEST_ASSERT_TRUE(ssidHas("UniFi Protect", CAM_SSID_KW));
}

void test_ssidHas_cam_all_keywords(void) {
    for (const char** kw = CAM_SSID_KW; *kw; kw++) {
        char ssid[64];
        snprintf(ssid, sizeof(ssid), "Test-%s-Suffix", *kw);
        TEST_ASSERT_TRUE_MESSAGE(ssidHas(ssid, CAM_SSID_KW), *kw);
    }
}

// ── ssidHas() — general behavior ──────────────────────────────────────────────

void test_ssidHas_case_insensitive(void) {
    TEST_ASSERT_TRUE(ssidHas("FLOCK-CAMERA-NET", FLOCK_SSID_KW));
    TEST_ASSERT_TRUE(ssidHas("flocksafety", FLOCK_SSID_KW));
    TEST_ASSERT_TRUE(ssidHas("RaVeN-unit", FLOCK_SSID_KW));
}

void test_ssidHas_no_match(void) {
    TEST_ASSERT_FALSE(ssidHas("MyHomeWiFi", FLOCK_SSID_KW));
    TEST_ASSERT_FALSE(ssidHas("Starbucks Guest", ALPR_SSID_KW));
    TEST_ASSERT_FALSE(ssidHas("Xfinity5G", CAM_SSID_KW));
}

void test_ssidHas_null_and_empty(void) {
    TEST_ASSERT_FALSE(ssidHas(nullptr, FLOCK_SSID_KW));
    TEST_ASSERT_FALSE(ssidHas("", FLOCK_SSID_KW));
}

// ── strContainsCI() — FLOCK_BLE_NAMES ────────────────────────────────────────

void test_strContainsCI_flock_ble_names(void) {
    for (const char** kw = FLOCK_BLE_NAMES; *kw; kw++) {
        char name[64];
        snprintf(name, sizeof(name), "Device-%s-01", *kw);
        TEST_ASSERT_TRUE_MESSAGE(strContainsCI(name, *kw), *kw);
    }
}

void test_strContainsCI_exact_and_substring(void) {
    TEST_ASSERT_TRUE(strContainsCI("Flock", "flock"));
    TEST_ASSERT_TRUE(strContainsCI("My Flock Device", "flock"));
    TEST_ASSERT_TRUE(strContainsCI("Raven Unit #5", "raven"));
    TEST_ASSERT_TRUE(strContainsCI("FS Ext Battery 12", "fs ext battery"));
}

void test_strContainsCI_case_insensitive(void) {
    // NOTE: strContainsCI() only lowercases the haystack (`hay`) side — every
    // real call site in main.cpp/es_detect.h passes an already-lowercase
    // `needle` (e.g. FLOCK_BLE_NAMES entries are all lowercase literals), so
    // that's the documented/actual contract this test verifies. A mixed- or
    // upper-case needle is a caller bug, not something this function handles.
    TEST_ASSERT_TRUE(strContainsCI("PENGUIN", "penguin"));
    TEST_ASSERT_TRUE(strContainsCI("Pigvision Controller", "pigvision"));
    TEST_ASSERT_TRUE(strContainsCI("FLOCK-SAFETY-CAM", "flock"));
}


void test_strContainsCI_no_match(void) {
    TEST_ASSERT_FALSE(strContainsCI("Random BLE Device", "flock"));
    TEST_ASSERT_FALSE(strContainsCI("iPhone 15", "raven"));
}

void test_strContainsCI_null_and_empty(void) {
    TEST_ASSERT_FALSE(strContainsCI(nullptr, "flock"));
    TEST_ASSERT_FALSE(strContainsCI("flock", nullptr));
    TEST_ASSERT_FALSE(strContainsCI("", "flock"));
}

// ── SKIMMER_NAMES — exact-match table sanity ──────────────────────────────────
// main.cpp compares BLE device names against SKIMMER_NAMES with `==` (exact
// match, not substring), so this just pins down the table's contents/order.

void test_skimmer_names_contents(void) {
    int count = 0;
    bool sawHc03 = false, sawHc05 = false, sawHc06 = false;
    for (const char** n = SKIMMER_NAMES; *n; n++) {
        count++;
        if (strcmp(*n, "HC-03") == 0) sawHc03 = true;
        if (strcmp(*n, "HC-05") == 0) sawHc05 = true;
        if (strcmp(*n, "HC-06") == 0) sawHc06 = true;
    }
    TEST_ASSERT_EQUAL_INT(3, count);
    TEST_ASSERT_TRUE(sawHc03);
    TEST_ASSERT_TRUE(sawHc05);
    TEST_ASSERT_TRUE(sawHc06);
}

// ── RAVEN_UUIDS — table sanity ────────────────────────────────────────────────
// 5 VENDOR-SPECIFIC GATT service UUIDs (GainSec research). The three standard
// Bluetooth SIG assignments that used to be listed here (0x180A Device
// Information, 0x1809 Health Thermometer, 0x1819 Location and Navigation) were
// removed because they are advertised by essentially every BLE device in
// existence — scoring them as Raven evidence makes ordinary hardware (fitness
// bands, watches, earbuds) register as surveillance equipment. This test
// previously asserted a count of 8 and checked 0x3100 positively.

void test_raven_uuids_count_and_contents(void) {
    int count = 0;
    for (const char** u = RAVEN_UUIDS; *u; u++) count++;
    TEST_ASSERT_EQUAL_INT(5, count);

    bool sawGps = false;
    for (const char** u = RAVEN_UUIDS; *u; u++) {
        if (strcmp(*u, "00003100-0000-1000-8000-00805f9b34fb") == 0) sawGps = true;
    }
    TEST_ASSERT_TRUE(sawGps);
}

// The standard SIG services must NOT be matchable as Raven evidence, whether by
// the named table or by the 16-bit range check.
void test_raven_standard_services_never_alert(void) {
    const char* std_uuids[] = {
        "0000180a-0000-1000-8000-00805f9b34fb",  // Device Information
        "00001809-0000-1000-8000-00805f9b34fb",  // Health Thermometer
        "00001819-0000-1000-8000-00805f9b34fb",  // Location and Navigation
    };
    for (int i = 0; i < 3; i++) {
        // Not in the alerting table...
        bool inTable = false;
        for (const char** u = RAVEN_UUIDS; *u; u++) {
            if (strcmp(*u, std_uuids[i]) == 0) inTable = true;
        }
        TEST_ASSERT_FALSE_MESSAGE(inTable, std_uuids[i]);
        // ...and not reachable through the range parser either.
        TEST_ASSERT_FALSE(ravenUuidInRange(std_uuids[i]));
    }
    // They are still retained as firmware-estimation evidence.
    int legacy = 0;
    for (const char** u = RAVEN_LEGACY_UUIDS; *u; u++) legacy++;
    TEST_ASSERT_EQUAL_INT(3, legacy);
}

// Regression guard: no entry in the alerting table may be a standard service,
// so re-adding one fails here rather than on a user's wrist.
void test_raven_table_has_no_standard_services(void) {
    for (const char** u = RAVEN_UUIDS; *u; u++) {
        int svc = ravenService16FromUuidString(*u);
        TEST_ASSERT_FALSE_MESSAGE(
            svc >= 0 && service16IsStandardSvc((uint16_t)svc), *u);
    }
}

void test_service16_standard_classifier(void) {
    TEST_ASSERT_TRUE(service16IsStandardSvc(0x1800));   // Generic Access
    TEST_ASSERT_TRUE(service16IsStandardSvc(0x1809));
    TEST_ASSERT_TRUE(service16IsStandardSvc(0x180A));
    TEST_ASSERT_TRUE(service16IsStandardSvc(0x1819));
    // Raven's vendor range is not a SIG assignment, so it stays matchable —
    // including 0x3101/0x3102, the GPS-leaking services.
    TEST_ASSERT_FALSE(service16IsStandardSvc(0x3100));
    TEST_ASSERT_FALSE(service16IsStandardSvc(0x3101));
    TEST_ASSERT_FALSE(service16IsStandardSvc(0x3102));
    TEST_ASSERT_FALSE(service16IsStandardSvc(0x3500));
}

// ── CVE-2025-59409 — the truncated "flck" spelling ───────────────────────────

void test_ssidHas_test_flck_cve(void) {
    // "test_flck" is the development Wi-Fi credential string Flock's
    // Falcon/Sparrow firmware shipped in production. It contains no "flock"
    // substring (f-l-c-k vs f-l-o-c-k), so without the dedicated "flck" keyword
    // such a camera is invisible to the SSID engine.
    TEST_ASSERT_TRUE(ssidHas("test_flck", FLOCK_SSID_KW));
    TEST_ASSERT_TRUE(ssidHas("flck", FLOCK_SSID_KW));
    // Transposed spelling is NOT a match — "flkc" != "flck".
    TEST_ASSERT_FALSE(ssidHas("TEST_FLKC", FLOCK_SSID_KW));
    // And "flck" did not become a catch-all.
    TEST_ASSERT_FALSE(ssidHas("flick", FLOCK_SSID_KW));      // f-l-i-c-k
    TEST_ASSERT_FALSE(ssidHas("MyHomeWiFi", FLOCK_SSID_KW));
}

// ── Flock BLE manufacturer company IDs ───────────────────────────────────────

void test_flock_ble_mfr_ids(void) {
    TEST_ASSERT_EQUAL_UINT(1u, (unsigned)NUM_FLOCK_BLE_MFR_IDS);
    TEST_ASSERT_EQUAL_UINT(0x09C8u, (unsigned)FLOCK_BLE_MFR_IDS[0]);
    TEST_ASSERT_TRUE(flockBleMfrIdMatch(0x09C8));
    TEST_ASSERT_FALSE(flockBleMfrIdMatch(0x05A7));   // pre-PR#39 wrong value
    TEST_ASSERT_FALSE(flockBleMfrIdMatch(0x0000));
}

// ── nullptr-termination sanity across all pattern arrays ─────────────────────
// Every array is walked by callers with a `for (...; *kw; kw++)` loop — an
// un-terminated array would read past the end and crash/UB on-device. This
// just confirms each array is reachable and terminates within a sane bound.

template <size_t MaxScan>
static bool isNullTerminatedWithin(const char** arr) {
    for (size_t i = 0; i < MaxScan; i++) {
        if (arr[i] == nullptr) return true;
    }
    return false;
}

void test_pattern_arrays_null_terminated(void) {
    TEST_ASSERT_TRUE(isNullTerminatedWithin<64>(FLOCK_SSID_KW));
    TEST_ASSERT_TRUE(isNullTerminatedWithin<64>(ALPR_SSID_KW));
    TEST_ASSERT_TRUE(isNullTerminatedWithin<64>(CAM_SSID_KW));
    TEST_ASSERT_TRUE(isNullTerminatedWithin<64>(FLOCK_BLE_NAMES));
    TEST_ASSERT_TRUE(isNullTerminatedWithin<64>(RAVEN_UUIDS));
    TEST_ASSERT_TRUE(isNullTerminatedWithin<64>(FLOCK_GATT_UUIDS));
    TEST_ASSERT_TRUE(isNullTerminatedWithin<64>(SKIMMER_NAMES));
}

// ── Firmware-derived additions (2026-09-16 dump) ──────────────────────────────

// BLE name *shapes* — the forms a substring keyword list cannot express, most
// notably a bare 10-digit serial, which contains no text to search for.
void test_ble_name_shape_penguin_serial(void) {
    TEST_ASSERT_TRUE(bleNameShapeMatch("Penguin-1234567890"));
    TEST_ASSERT_TRUE(bleNameShapeMatch("PENGUIN-0000000000"));
}

void test_ble_name_shape_bare_serial(void) {
    TEST_ASSERT_TRUE(bleNameShapeMatch("1234567890"));
    // The substring keyword list alone cannot see this form...
    TEST_ASSERT_FALSE(strContainsCI("1234567890", "penguin"));
    // ...so the detector relies on the shape matcher for it.
    TEST_ASSERT_TRUE(bleNameShapeMatch("9876543210"));
}

void test_ble_name_shape_dfutarg(void) {
    TEST_ASSERT_TRUE(bleNameShapeMatch("DfuTarg"));
    TEST_ASSERT_TRUE(bleNameShapeMatch("dfutarg"));
}

void test_ble_name_shape_fs_ext_battery_exact(void) {
    TEST_ASSERT_TRUE(bleNameShapeMatch("FS Ext Battery"));
    TEST_ASSERT_TRUE(bleNameShapeMatch("fs ext battery"));
}

void test_ble_name_shape_rejects_wrong_digit_counts(void) {
    TEST_ASSERT_FALSE(bleNameShapeMatch("123456789"));     // 9 digits
    TEST_ASSERT_FALSE(bleNameShapeMatch("12345678901"));   // 11 digits
    TEST_ASSERT_FALSE(bleNameShapeMatch("1234567890X"));   // trailing junk
    TEST_ASSERT_FALSE(bleNameShapeMatch("Penguin-123456789"));
    TEST_ASSERT_FALSE(bleNameShapeMatch("Penguin-abcdefghij"));
}

void test_ble_name_shape_no_match(void) {
    TEST_ASSERT_FALSE(bleNameShapeMatch("JBL Flip 5"));
    TEST_ASSERT_FALSE(bleNameShapeMatch(""));
    TEST_ASSERT_FALSE(bleNameShapeMatch(nullptr));
}

void test_ciEquals(void) {
    TEST_ASSERT_TRUE (ciEquals("FS Ext Battery", "fs ext battery"));
    TEST_ASSERT_TRUE (ciEquals("", ""));
    TEST_ASSERT_FALSE(ciEquals("abc", "abcd"));
    TEST_ASSERT_FALSE(ciEquals("abc", "abd"));
    TEST_ASSERT_FALSE(ciEquals(nullptr, "x"));
}

// Flock accessory + Nordic legacy DFU GATT services (firmware dump).
void test_flock_gatt_uuids_contents(void) {
    TEST_ASSERT_EQUAL_STRING(FLOCK_ACCESSORY_UUID, FLOCK_GATT_UUIDS[0]);
    TEST_ASSERT_EQUAL_STRING(NORDIC_DFU_UUID,      FLOCK_GATT_UUIDS[1]);
    TEST_ASSERT_NULL(FLOCK_GATT_UUIDS[2]);
    // The Flock accessory service must NOT be presented as a Raven UUID — they
    // are deliberately separate detectors/tags.
    bool foundInRaven = false;
    for (const char** u = RAVEN_UUIDS; *u; u++)
        if (strcmp(*u, FLOCK_ACCESSORY_UUID) == 0) foundInRaven = true;
    TEST_ASSERT_FALSE(foundInRaven);
}

// Raven service RANGE (0x3100-0x3500). The named table only holds the round
// hundred values; 0x3101/0x3102 — the unauthenticated services that leak GPS —
// are NOT in it, so the range check is what catches them.
void test_raven_service_range_bounds(void) {
    TEST_ASSERT_TRUE (ravenServiceInRange(0x3100));
    TEST_ASSERT_TRUE (ravenServiceInRange(0x3101));
    TEST_ASSERT_TRUE (ravenServiceInRange(0x3102));
    TEST_ASSERT_TRUE (ravenServiceInRange(0x3500));
    TEST_ASSERT_FALSE(ravenServiceInRange(0x30ff));
    TEST_ASSERT_FALSE(ravenServiceInRange(0x3501));
}

void test_raven_uuid_in_range_parsing(void) {
    // Canonical 128-bit form (what NimBLEUUID::toString() emits)…
    TEST_ASSERT_TRUE (ravenUuidInRange("00003101-0000-1000-8000-00805f9b34fb"));
    TEST_ASSERT_TRUE (ravenUuidInRange("00003102-0000-1000-8000-00805f9b34fb"));
    // …and the short forms.
    TEST_ASSERT_TRUE (ravenUuidInRange("0x3101"));
    TEST_ASSERT_TRUE (ravenUuidInRange("3101"));
    // The 16-bit value is the LOW half of the first group: "00003101" is 0x3101,
    // not 0x0000.
    TEST_ASSERT_EQUAL_INT(0x3101, ravenService16FromUuidString("00003101-0000-1000-8000-00805f9b34fb"));
    // Out of range, non-Bluetooth-base, and junk.
    TEST_ASSERT_FALSE(ravenUuidInRange("000030ff-0000-1000-8000-00805f9b34fb"));
    TEST_ASSERT_FALSE(ravenUuidInRange("00003600-0000-1000-8000-00805f9b34fb"));
    TEST_ASSERT_FALSE(ravenUuidInRange(FLOCK_ACCESSORY_UUID));
    TEST_ASSERT_FALSE(ravenUuidInRange("1b7e"));   // old short-form Raven id
    TEST_ASSERT_FALSE(ravenUuidInRange("fd60"));
    TEST_ASSERT_FALSE(ravenUuidInRange(nullptr));
}

// ─────────────────────────────────────────────────────────────────────────────

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_ssidHas_flock_keywords);
    RUN_TEST(test_ssidHas_flock_all_keywords);
    RUN_TEST(test_ssidHas_alpr_keywords);
    RUN_TEST(test_ssidHas_alpr_all_keywords);
    RUN_TEST(test_ssidHas_cam_keywords);
    RUN_TEST(test_ssidHas_cam_all_keywords);
    RUN_TEST(test_ssidHas_case_insensitive);
    RUN_TEST(test_ssidHas_no_match);
    RUN_TEST(test_ssidHas_null_and_empty);

    RUN_TEST(test_strContainsCI_flock_ble_names);
    RUN_TEST(test_strContainsCI_exact_and_substring);
    RUN_TEST(test_strContainsCI_case_insensitive);
    RUN_TEST(test_strContainsCI_no_match);
    RUN_TEST(test_strContainsCI_null_and_empty);

    RUN_TEST(test_skimmer_names_contents);
    RUN_TEST(test_raven_uuids_count_and_contents);
    RUN_TEST(test_raven_standard_services_never_alert);
    RUN_TEST(test_raven_table_has_no_standard_services);
    RUN_TEST(test_service16_standard_classifier);
    RUN_TEST(test_ssidHas_test_flck_cve);
    RUN_TEST(test_flock_ble_mfr_ids);
    RUN_TEST(test_pattern_arrays_null_terminated);

    RUN_TEST(test_ble_name_shape_penguin_serial);
    RUN_TEST(test_ble_name_shape_bare_serial);
    RUN_TEST(test_ble_name_shape_dfutarg);
    RUN_TEST(test_ble_name_shape_fs_ext_battery_exact);
    RUN_TEST(test_ble_name_shape_rejects_wrong_digit_counts);
    RUN_TEST(test_ble_name_shape_no_match);
    RUN_TEST(test_ciEquals);
    RUN_TEST(test_flock_gatt_uuids_contents);
    RUN_TEST(test_raven_service_range_bounds);
    RUN_TEST(test_raven_uuid_in_range_parsing);

    return UNITY_END();
}
