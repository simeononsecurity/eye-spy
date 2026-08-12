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
//     SOUNDTHINKING_OUIS/NUM_SOUNDTHINKING_OUIS, ALPR_OUIS/NUM_ALPR_OUIS,
//     FLOCK_SSID_KW, ALPR_SSID_KW, CAM_SSID_KW, FLOCK_BLE_NAMES,
//     RAVEN_UUIDS, SKIMMER_NAMES
//   Matching helpers: ouiMatch(), ssidHas(), strContainsCI()

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
};
#define NUM_FLOCK_MFR_OUIS (sizeof(FLOCK_MFR_OUIS)/sizeof(FLOCK_MFR_OUIS[0]))

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

// Flock / Raven BLE device name patterns (case-insensitive substring)
static const char* FLOCK_BLE_NAMES[] = {
    "flock", "raven", "penguin", "pigvision", "fs ext battery", nullptr
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
