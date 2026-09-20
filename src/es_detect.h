// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2024 SimeonOnSecurity <https://github.com/simeononsecurity>
//
// es_detect.h — Eye Spy detection pattern tables + pure matching helpers
//
// WHY THIS FILE EXISTS (de-monolithic refactor, mirrors flock-you-esp32's
// fy_detect.h): all of the OUI/SSID/BLE-name/UUID pattern data, plus the
// pure string/byte matching functions that consume it, are hardware- and
// state-independent — they take plain buffers/strings in and return a
// bool/match result out, with no Arduino/ESP-IDF calls, no globals, and no
// side effects. Isolating them here means:
//   1. Adding/editing a pattern (e.g. a new Flock OUI) cannot accidentally
//      break scoring or phase-state logic, and vice versa.
//   2. These functions are trivially unit-testable on a host machine (no
//      ESP32 hardware required) if/when a native test target is added —
//      the same role flock-you-esp32's fy_detect.h plays for its
//      test/test_ble_matching and test/test_uuid_matching suites.
//
// Public interface expected by main.cpp:
//   Pattern tables: CAM_OUIS/NUM_CAM_OUIS, FLOCK_OUIS/NUM_FLOCK_OUIS,
//     FLOCK_MFR_OUIS/NUM_FLOCK_MFR_OUIS,
//     FLOCK_FW_DEFAULT_MACS/NUM_FLOCK_FW_DEFAULT_MACS,
//     SOUNDTHINKING_OUIS/NUM_SOUNDTHINKING_OUIS, ALPR_OUIS/NUM_ALPR_OUIS,
//     FLOCK_SSID_KW, ALPR_SSID_KW, CAM_SSID_KW, FLOCK_BLE_NAMES,
//     RAVEN_UUIDS, RAVEN_SVC_MIN/RAVEN_SVC_MAX, FLOCK_GATT_UUIDS,
//     SKIMMER_NAMES
//   Matching helpers: ouiMatch(), ssidHas(), strContainsCI(), ciEquals(),
//     fwDefaultMacMatch(), bleNameShapeMatch(), ravenServiceInRange(),
//     ravenService16FromUuidString(), ravenUuidInRange()

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cctype>

// ── Camera vendor OUIs (scored +3, caution-level) ────────────────────────────
static const uint8_t CAM_OUIS[][3] = {
    {0x00,0x40,0x8c},{0xac,0xcc,0x8e},{0xb8,0xa4,0x4f}, // Axis
    {0x4c,0xbd,0x8f},{0xbc,0xad,0x28},{0x44,0x19,0xb6},  // Hikvision
    {0xc4,0x2f,0x90},{0x28,0x57,0xbe},                  // Hikvision
    {0x90,0x02,0xa9},{0x3c,0xef,0x8c},{0xe0,0x50,0x8b},{0x4c,0x11,0xbf}, // Dahua
    {0xec,0x71,0xdb},                                   // Reolink
    {0x2c,0xaa,0x8e},{0xd0,0x3f,0x27},                  // Wyze
    {0x3c,0x37,0x86},{0x14,0xb4,0x84},                  // Arlo
    {0xf0,0x27,0x65},{0x18,0xb4,0x30},                  // Ring/Nest
    {0x64,0x16,0x66},                                   // Nest
    {0x9c,0x8e,0xcd},{0x90,0xc7,0xd8},                  // Amcrest
    {0x00,0x2b,0x67},                                   // Vivotek
    {0x34,0x40,0xb5},{0x00,0x09,0x6c},                  // Hanwha
    {0x00,0x40,0x48},{0xac,0x3a,0x7a},                  // FLIR
    {0x00,0x1b,0xc5},                                   // Mobotix
    {0xa8,0x9f,0xba},{0xfc,0xec,0xda},{0x24,0xa4,0x3c}, // Ubiquiti
};
#define NUM_CAM_OUIS (sizeof(CAM_OUIS)/sizeof(CAM_OUIS[0]))

// Flock Safety camera OUIs — scored +5 (high-confidence, exclusively Flock).
// Synced with flock-you-esp32's fy_detect.h fy_oui_high[].
// NOTE: 82:6b:f2 has the LAA bit set (0x82 & 0x02) but IS a confirmed Flock OUI
//       (DeFlockJoplin 12th camera, wildcard-probe field dataset).
static const uint8_t FLOCK_OUIS[][3] = {
    // @NitekryDPaul promiscuous-mode dataset (25 OUIs)
    {0x70,0xc9,0x4e},{0x3c,0x91,0x80},{0xd8,0xf3,0xbc},{0x80,0x30,0x49},{0xb8,0x35,0x32},
    {0x14,0x5a,0xfc},{0x74,0x4c,0xa1},{0x08,0x3a,0x88},{0x9c,0x2f,0x9d},{0xc0,0x35,0x32},
    {0x94,0x08,0x53},{0xe4,0xaa,0xea},{0x24,0xb2,0xb9},
    {0xb8,0x1e,0xa4},{0x70,0x08,0x94},{0x58,0x8e,0x81},{0xec,0x1b,0xbd},{0x3c,0x71,0xbf},
    {0x58,0x00,0xe3},{0x90,0x35,0xea},{0x5c,0x93,0xa2},{0x64,0x6e,0x69},{0x48,0x27,0xea},
    {0xa4,0xcf,0x12},{0xe0,0x4f,0x43},
    // DeFlockJoplin — 12th cam, wildcard-probe field test (LAA bit set but confirmed Flock)
    {0x82,0x6b,0xf2},
    // Flock Safety direct IEEE assignment (dougborg/PR#39)
    {0xb4,0x1e,0x52},
    // FS Ext Battery device series (dougborg/PR#39)
    {0x04,0x0d,0x84},{0xf0,0x82,0xc0},{0x1c,0x34,0xf1},{0x38,0x5b,0x44},{0x94,0x34,0x69},{0xb4,0xe3,0xf9},
    // Legacy eye-spy entries (IEEE-registered, kept for continuity)
    {0xd4,0xbb,0xe6},{0x3c,0x61,0x05},
};
#define NUM_FLOCK_OUIS (sizeof(FLOCK_OUIS)/sizeof(FLOCK_OUIS[0]))

// Contract-manufacturer OUIs (Liteon Technology / USI) — low confidence (+2).
// These ship many non-Flock devices; match alone warrants caution, not alert.
static const uint8_t FLOCK_MFR_OUIS[][3] = {
    {0xf4,0x6a,0xdd},  // Liteon Technology
    {0xf8,0xa2,0xd6},  // Liteon Technology
    {0x00,0xf4,0x8d},  // Universal Scientific Industrial (USI)
    {0xd0,0x39,0x57},  // USI
    {0xe8,0xd0,0xfc},  // USI
    {0xe0,0x0a,0xf6},  // USI (dougborg/PR#39)
    // Qualcomm Atheros — the QCA9377 is the radio in Flock's MSM8953-generation
    // cameras (firmware dump, 2026-09-16). Deliberately LOW tier, not in
    // FLOCK_OUIS above: unlike b4:1e:52, this is a chipset vendor's prefix with
    // a huge installed base of unrelated Atheros gear, so the OUI alone is not
    // evidence of a camera. The two *factory-default* MACs the camera ships
    // with from this block ARE specific and are checked exactly, at high
    // confidence, by fwDefaultMacMatch() (see FLOCK_FW_DEFAULT_MACS below).
    {0x00,0x03,0x7f},  // Qualcomm Atheros QCA9377 (firmware dump, 2026-09-16)
};
#define NUM_FLOCK_MFR_OUIS (sizeof(FLOCK_MFR_OUIS)/sizeof(FLOCK_MFR_OUIS[0]))

// ── Firmware-default radio MACs (full 6 bytes, exact match) ──────────────────
// The *factory-default* QCA9377 radio addresses baked into the Flock camera
// firmware image (MSM8953 + QCA9377 dump, 2026-09-16):
//   00:03:7f:50:00:01 — bdwlan30.bin / fakeboar.bin (WLAN NVRAM)
//   00:03:7f:4f:00:16 — otp30.bin                   (OTP / factory partition)
//
// WHY a separate exact-match table when 00:03:7f is already in the mfr list:
// the *OUI* belongs to every other Atheros device on earth, but these two
// *complete* addresses are what the radio transmits while still unprovisioned
// (Flock's provisioning step rewrites the MAC). A 6-byte match on a factory
// default is therefore near-zero-false-positive — hence PTS_FW_DEFAULT_MAC
// scores it far above the +2 mfr-tier OUI hit. Corollary: this only ever fires
// on a freshly-imaged / never-provisioned unit.
static const uint8_t FLOCK_FW_DEFAULT_MACS[][6] = {
    {0x00,0x03,0x7f,0x50,0x00,0x01},  // bdwlan30.bin / fakeboar.bin default
    {0x00,0x03,0x7f,0x4f,0x00,0x16},  // otp30.bin default
};
#define NUM_FLOCK_FW_DEFAULT_MACS (sizeof(FLOCK_FW_DEFAULT_MACS)/sizeof(FLOCK_FW_DEFAULT_MACS[0]))

// SoundThinking / ShotSpotter acoustic sensors — co-deployed with Flock ALPR (+4).
static const uint8_t SOUNDTHINKING_OUIS[][3] = {
    {0xd4,0x11,0xd6},  // SoundThinking (formerly ShotSpotter)
};
#define NUM_SOUNDTHINKING_OUIS (sizeof(SOUNDTHINKING_OUIS)/sizeof(SOUNDTHINKING_OUIS[0]))

// ALPR OUIs — Motorola Solutions / Vigilant Solutions LPR cameras
static const uint8_t ALPR_OUIS[][3] = {
    {0x00,0x0e,0x58},  // Motorola Solutions / Vigilant
};
#define NUM_ALPR_OUIS (sizeof(ALPR_OUIS)/sizeof(ALPR_OUIS[0]))

// Flock-specific SSID keywords (score +5)
static const char* FLOCK_SSID_KW[] = {
    "flock", "flocksafety", "fs ext", "penguin", "pigvision", "raven", nullptr
};

// General ALPR keyword SSIDs (score +4)
static const char* ALPR_SSID_KW[] = {
    "alpr", "lpr", "vigilant", "plateread", "licenseplat",
    "motorola", "automate", nullptr
};

// Camera keyword SSIDs (score +2)
static const char* CAM_SSID_KW[] = {
    "cam", "ipcam", "hikvision", "dahua", "reolink", "arlo", "wyze",
    "ring", "blink", "nest", "cctv", "nvr", "dvr", "doorbell",
    "surv", "axis", "amcrest", "vivotek", "lorex", "unifi", "protect",
    "flir", "mobotix", "hanwha", "genetec", nullptr
};

// Flock / Raven BLE device name patterns (case-insensitive substring).
// "dfutarg" (Nordic legacy-DFU target) added from the firmware-derived set
// (2026-09-16): the Penguin battery pack advertises it while receiving a
// firmware update — the image bundles no.nordicsemi.android.dfu.
static const char* FLOCK_BLE_NAMES[] = {
    "flock", "raven", "penguin", "pigvision", "fs ext battery", "dfutarg", nullptr
};

// Raven GATT service UUIDs (GainSec research — full 128-bit)
static const char* RAVEN_UUIDS[] = {
    "0000180a-0000-1000-8000-00805f9b34fb",  // Device Information
    "00003100-0000-1000-8000-00805f9b34fb",  // GPS
    "00003200-0000-1000-8000-00805f9b34fb",  // Power
    "00003300-0000-1000-8000-00805f9b34fb",  // Network
    "00003400-0000-1000-8000-00805f9b34fb",  // Upload
    "00003500-0000-1000-8000-00805f9b34fb",  // Error
    "00001809-0000-1000-8000-00805f9b34fb",  // Health (legacy fw 1.1.x)
    "00001819-0000-1000-8000-00805f9b34fb",  // Location (legacy fw 1.1.x)
    nullptr
};

// Raven camera services are advertised across the whole 16-bit range
// 0x3100-0x3500, not just the round hundred values listed above. The services
// that matter most are the ones NOT in that list: 0x3101 / 0x3102 expose GPS
// latitude/longitude unauthenticated (firmware dump, 2026-09-16). Exact-string
// matching alone therefore missed precisely the highest-value services, which
// is what the range check in ravenServiceInRange() closes.
#define RAVEN_SVC_MIN 0x3100
#define RAVEN_SVC_MAX 0x3500

// Flock accessory / Nordic DFU GATT services (firmware dump, 2026-09-16).
// Flock's own accessory service — exposed by the Penguin battery packs — plus
// the Nordic legacy DFU service the pack advertises while being flashed.
// Kept in their own table (and given their own detector/tag) rather than being
// appended to RAVEN_UUIDS[], because they are *not* Raven services and
// reporting them as "Raven-BLE-UUID" would mislabel them on the dashboard.
#define FLOCK_ACCESSORY_UUID "e8ccbb38-9532-46a8-9fe5-1814df172e6f"
#define NORDIC_DFU_UUID      "00001530-1212-efde-1523-785feabcd123"

static const char* FLOCK_GATT_UUIDS[] = {
    FLOCK_ACCESSORY_UUID,
    NORDIC_DFU_UUID,
    nullptr
};

// HC-0x card skimmer names (exact match)
static const char* SKIMMER_NAMES[] = { "HC-03", "HC-05", "HC-06", nullptr };

// ── Pure matching helpers ─────────────────────────────────────────────────

// Case-insensitive substring search. `hay` is copied into a 64-byte scratch
// buffer (truncated if longer) — fine for BLE device names, which are short.
static inline bool strContainsCI(const char* hay, const char* needle) {
    if (!hay || !needle) return false;
    char low[64]; size_t i=0;
    while (i<63 && hay[i]) { low[i]=(char)tolower((unsigned char)hay[i]); i++; }
    low[i]='\0';
    return strstr(low, needle) != nullptr;
}

// Returns true if `bssid`'s first 3 bytes match any OUI in `tbl` (cnt entries).
static inline bool ouiMatch(const uint8_t* bssid, const uint8_t tbl[][3], size_t cnt) {
    for (size_t i = 0; i < cnt; i++)
        if (bssid[0]==tbl[i][0] && bssid[1]==tbl[i][1] && bssid[2]==tbl[i][2])
            return true;
    return false;
}

// Returns true if `ssid` contains (case-insensitively) any keyword in the
// nullptr-terminated `kws` array. SSID truncated to 32 chars per 802.11 spec.
static inline bool ssidHas(const char* ssid, const char** kws) {
    if (!ssid || !ssid[0]) return false;
    char low[33]; size_t i=0;
    while (i<32 && ssid[i]) { low[i]=(char)tolower((unsigned char)ssid[i]); i++; }
    low[i]='\0';
    for (const char** kw=kws; *kw; kw++)
        if (strstr(low,*kw)) return true;
    return false;
}

// ── Firmware-derived matching helpers (Flock camera firmware dump, 2026-09-16) ─

// Exact match on a full 6-byte firmware-default radio MAC (see
// FLOCK_FW_DEFAULT_MACS). Applied to a scanned BSSID.
static inline bool fwDefaultMacMatch(const uint8_t* mac6) {
    if (!mac6) return false;
    for (size_t i = 0; i < NUM_FLOCK_FW_DEFAULT_MACS; i++) {
        if (memcmp(mac6, FLOCK_FW_DEFAULT_MACS[i], 6) == 0) return true;
    }
    return false;
}

// Case-insensitive string equality, hand-rolled rather than using strcasecmp:
// strcasecmp is POSIX, not ISO C, so pulling it in would make this header
// (which must compile identically for the ESP32 target and the host test
// build) depend on _GNU_SOURCE.
static inline bool ciEquals(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

// BLE device-name *shape* match — the firmware-derived naming forms a substring
// keyword list cannot express, because a bare serial number contains no
// distinguishing text to search for:
//   "Penguin-NNNNNNNNNN" — exactly 10 digits after the dash
//   "NNNNNNNNNN"         — a bare 10-digit serial, nothing else
//   "FS Ext Battery"     — exact (case-insensitive)
//   "DfuTarg"            — exact; Nordic legacy DFU target, advertised by the
//                          Penguin pack while it is being firmware-updated
static inline bool bleNameShapeMatch(const char* name) {
    if (!name || !name[0]) return false;
    if (ciEquals(name, "fs ext battery")) return true;
    if (ciEquals(name, "dfutarg"))        return true;

    // "penguin-" prefix (any case) + exactly 10 digits + end of string.
    static const char* kPrefix = "penguin-";
    size_t pi = 0;
    while (kPrefix[pi] && name[pi] &&
           tolower((unsigned char)name[pi]) == kPrefix[pi]) pi++;
    if (kPrefix[pi] == '\0') {
        const char* p = name + pi;
        int d = 0;
        while (p[d] >= '0' && p[d] <= '9') d++;
        if (d == 10 && p[10] == '\0') return true;
    }

    // Bare 10-digit serial.
    {
        int d = 0;
        while (name[d] >= '0' && name[d] <= '9') d++;
        if (d == 10 && name[10] == '\0') return true;
    }
    return false;
}

// True when a 16-bit service UUID falls in the Raven camera range.
static inline bool ravenServiceInRange(uint16_t svc16) {
    return svc16 >= RAVEN_SVC_MIN && svc16 <= RAVEN_SVC_MAX;
}

// Extracts the 16-bit service value from either UUID string shape NimBLE hands
// us: the canonical "00003101-0000-1000-8000-00805f9b34fb" expansion, or a
// short "0x3101" / "3101". Returns -1 when the string is neither (e.g. a real
// 128-bit vendor UUID). Hand-rolled hex parsing keeps this header free of
// <cstdlib>/strtol.
static inline int ravenService16FromUuidString(const char* uuid) {
    if (!uuid) return -1;
    while (*uuid == ' ') uuid++;
    if (uuid[0] == '0' && (uuid[1] == 'x' || uuid[1] == 'X')) uuid += 2;

    static const char* kBase = "-0000-1000-8000-00805f9b34fb";
    const size_t n = strlen(uuid);

    if (n != 4 && !(n == 8 + strlen(kBase) && ciEquals(uuid + 8, kBase)))
        return -1;

    // 16-bit value = LOW half of the 8-hex-digit group ("00003101" → 0x3101).
    const int digits = (n == 4) ? 4 : 8;
    int v = 0;
    for (int i = 0; i < digits; i++) {
        char c = uuid[i];
        int h = (c >= '0' && c <= '9') ? c - '0'
              : (c >= 'a' && c <= 'f') ? c - 'a' + 10
              : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
        if (h < 0) return -1;
        v = (v << 4) | h;
    }
    return v & 0xFFFF;
}

// Convenience wrapper: does this UUID string denote an in-range Raven service?
static inline bool ravenUuidInRange(const char* uuid) {
    const int svc = ravenService16FromUuidString(uuid);
    return svc >= 0 && ravenServiceInRange((uint16_t)svc);
}
