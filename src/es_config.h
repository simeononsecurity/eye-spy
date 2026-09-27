// es_config.h — user configuration blob (web-flasher configurator)
//
// Eye Spy counterpart of flock-you-esp32's fy_config.h. See
// docs/adr/0001-config-partition-and-flasher-configurator.md for the decision.
//
// The flasher builds these exact bytes in JS and writes them to the `escfg`
// partition as an extra ESP Web Tools manifest part. No filesystem, no NVS page
// format — a flat struct that cannot be "unreadable", only valid or not.
//
// THE MOST IMPORTANT PROPERTY: absent or invalid config is NOT an error. A blank
// (erased = 0xFF) or bad-CRC partition means "no user configuration", and every
// accessor returns the compile-time default, which is exactly today's behaviour.
// A device flashed without the configurator therefore behaves identically to one
// flashed before this file existed.
//
// Free of Arduino/ESP-IDF dependencies (the caller reads the partition and hands
// the bytes in) so the whole codec is unit-tested on the host — test/test_config/.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

// "ESCF" — identifies an Eye Spy config blob. A different project must use a
// different magic so a stray blob is ignored rather than misread (flock uses
// "FYCF").
#define ESCFG_MAGIC      0x46435345u
#define ESCFG_VERSION    1
#define ESCFG_TOTAL_LEN  64
#define ESCFG_CRC_OFFSET 60            // crc32 over bytes [0, 60)

// Output enables.
#define ESCFG_FLAG_LED     (1u << 0)   // status LED / screen flashes
#define ESCFG_FLAG_CHIRP   (1u << 1)   // audible alerts (speaker or buzzer)
#define ESCFG_FLAG_VIBRATE (1u << 2)   // vibration motor (Core2 For AWS only)
#define ESCFG_FLAG_HOLD    (1u << 3)   // hold a critical alert on the display
// bits 4..15 reserved
#define ESCFG_FLAG_ALL_OUTPUTS (ESCFG_FLAG_LED | ESCFG_FLAG_CHIRP | \
                                ESCFG_FLAG_VIBRATE | ESCFG_FLAG_HOLD)

// Defaults = today's behaviour, single source of truth. The config layer may
// only OVERRIDE these, never redefine them, so there is no second copy to drift.
#define ESCFG_DEFAULT_FLAGS     ESCFG_FLAG_ALL_OUTPUTS
#define ESCFG_DEFAULT_ENGINES   0xFFFFFFFFu   // every detection enabled
#define ESCFG_DEFAULT_MIN_SCORE 6              // SCORE_ALERT
#define ESCFG_DEFAULT_RSSI_FLOOR (-100)        // accept everything (RSSI_MIN today)
#define ESCFG_DEFAULT_TRACKER_MIN 30           // the 30-minute following window

// ── Engine bits ──────────────────────────────────────────────────────────────
// One bit per detection engine, in a FIXED order that must match the flasher's
// ENGINE_GROUPS in docs/index.html. Bit indices are part of the wire contract:
// renumbering one silently changes what a user's saved configuration means, so
// append new engines at the end and never reorder.
enum {
    ES_ENG_AXON = 0,        // body cameras
    ES_ENG_RAYBAN,          // RayBan-Meta glasses
    ES_ENG_FLOCK_BLE_NAME,
    ES_ENG_FLOCK_BLE_MFR,   // XUNTONG battery packs
    ES_ENG_RAVEN_BLE,       // named Raven services
    ES_ENG_FLOCK_GATT,      // Flock accessory / Nordic DFU
    ES_ENG_SKIMMER,         // card-skimmer Bluetooth modules
    ES_ENG_AIRTAG,
    ES_ENG_ODID_BLE,        // drones
    ES_ENG_ODID_WIFI,
    ES_ENG_SMARTTAG,
    ES_ENG_TILE,
    ES_ENG_MESHCORE,
    ES_ENG_IBEACON,
    ES_ENG_PERSIST,         // unknown device seen repeatedly over time
    ES_ENG_FLOCK_OUI,       // Wi-Fi: Flock-registered / field-confirmed prefix
    ES_ENG_FLOCK_MFR_OUI,   // Wi-Fi: shared contract-manufacturer prefix
    ES_ENG_SOUNDTHINKING,   // Wi-Fi: ShotSpotter-class sensors
    ES_ENG_ALPR_OUI,        // Wi-Fi: other ALPR vendors
    ES_ENG_FLOCK_SSID,      // Wi-Fi: network named like Flock gear
    ES_ENG_ALPR_SSID,
    ES_ENG_CAM_OUI,         // Wi-Fi: generic camera vendors
    ES_ENG_CAM_SSID,
    ES_ENG_FW_DEFAULT_MAC,  // Wi-Fi: unprovisioned camera's factory MAC
    ES_ENG_COUNT
};
#define ESCFG_MAX_ENGINE 31

// Bounds. The MIN_SCORE floor is load-bearing: SCORE_ALERT (6) is what keeps the
// +5-tier signals — including the shared-manufacturer OUI, which is first-hand
// evidence that a Flock contractor built the hardware but NOT that it is a camera
// — from alerting on their own. Lowering it would recreate exactly the false
// positives this project has already fixed three times (each presenting as a
// status LED stuck red, because re-triggering the flash outran its own expiry).
// Users may therefore make the device QUIETER, never more alarm-prone.
#define ESCFG_MIN_MIN_SCORE  6
#define ESCFG_MAX_MIN_SCORE  12
#define ESCFG_MIN_RSSI_FLOOR (-100)
#define ESCFG_MAX_RSSI_FLOOR (-40)
#define ESCFG_MIN_TRACKER_MIN 5
#define ESCFG_MAX_TRACKER_MIN 120

typedef struct {
    uint16_t flags;        // ESCFG_FLAG_*
    uint32_t engines;      // bit per engine (ES_ENG_*)
    uint8_t  minScore;     // minimum score that may alert
    int8_t   rssiFloor;    // ignore BLE detections weaker than this (dBm)
    uint8_t  trackerMin;   // minutes of continuous presence before a tracker alerts
    uint8_t  reserved;
} EsConfig;

// Defined once in main.cpp; declared here so the UI task and the display headers
// can gate their own outputs. Never written after setup(), which is what makes
// reading them from another task safe without a lock.
extern EsConfig g_cfg;
extern bool     g_cfgLoaded;

// ── CRC32 (IEEE 802.3, reflected, poly 0xEDB88320) ───────────────────────────
// Bitwise rather than table-driven: it runs once at boot, and the JS in
// docs/index.html implements the identical algorithm — keeping both short and
// obviously-matching matters more than speed.
static inline uint32_t esCfgCrc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint32_t)data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

// ── Defaults / clamping ──────────────────────────────────────────────────────

static inline void esCfgDefaults(EsConfig* c) {
    memset(c, 0, sizeof(*c));
    c->flags      = ESCFG_DEFAULT_FLAGS;
    c->engines    = ESCFG_DEFAULT_ENGINES;
    c->minScore   = ESCFG_DEFAULT_MIN_SCORE;
    c->rssiFloor  = ESCFG_DEFAULT_RSSI_FLOOR;
    c->trackerMin = ESCFG_DEFAULT_TRACKER_MIN;
}

static inline uint8_t esCfgClampU8(uint8_t v, uint8_t lo, uint8_t hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}
static inline int8_t esCfgClamp8(int v, int lo, int hi) {
    return (int8_t)((v < lo) ? lo : (v > hi) ? hi : v);
}

// Clamp every field into the range the scoring model assumes. Applied on decode
// AND after any edit, so a hand-crafted blob cannot smuggle in a value the
// firmware would otherwise trust — see the ESCFG_MIN_MIN_SCORE note above.
static inline void esCfgClamp(EsConfig* c) {
    c->minScore   = esCfgClampU8(c->minScore,   ESCFG_MIN_MIN_SCORE,   ESCFG_MAX_MIN_SCORE);
    c->rssiFloor  = esCfgClamp8((int)c->rssiFloor, ESCFG_MIN_RSSI_FLOOR, ESCFG_MAX_RSSI_FLOOR);
    c->trackerMin = esCfgClampU8(c->trackerMin, ESCFG_MIN_TRACKER_MIN, ESCFG_MAX_TRACKER_MIN);
    c->flags     &= ESCFG_FLAG_ALL_OUTPUTS;   // drop reserved bits
    // engines: any subset is legal — that is the user's actual choice.
}

// ── Field accessors (defaults when no config is loaded) ──────────────────────

static inline bool esCfgOutputEnabled(const EsConfig* c, bool loaded, uint16_t flag) {
    if (!loaded) return (ESCFG_DEFAULT_FLAGS & flag) != 0;
    return (c->flags & flag) != 0;
}
static inline uint32_t esCfgEngines(const EsConfig* c, bool loaded) {
    return loaded ? c->engines : ESCFG_DEFAULT_ENGINES;
}
static inline uint8_t esCfgMinScore(const EsConfig* c, bool loaded) {
    return loaded ? c->minScore : ESCFG_DEFAULT_MIN_SCORE;
}
static inline int8_t esCfgRssiFloor(const EsConfig* c, bool loaded) {
    return loaded ? c->rssiFloor : ESCFG_DEFAULT_RSSI_FLOOR;
}
static inline uint8_t esCfgTrackerMin(const EsConfig* c, bool loaded) {
    return loaded ? c->trackerMin : ESCFG_DEFAULT_TRACKER_MIN;
}
// Is this engine enabled? Unknown bit indices (> ES_ENG_COUNT) are treated as
// enabled so a future engine is never silently disabled by an old config.
static inline bool esCfgEngineOn(const EsConfig* c, bool loaded, uint32_t bit) {
    if (bit >= ESCFG_MAX_ENGINE) return true;
    return ((loaded ? c->engines : ESCFG_DEFAULT_ENGINES) & (1u << bit)) != 0;
}

// ── Decode / encode ──────────────────────────────────────────────────────────

// Decode a raw partition image. Returns true when a valid blob was found; on ANY
// failure the destination is filled with defaults and false is returned, so the
// caller can log "using defaults" rather than reporting an error the user cannot
// act on. Failure is normal: an unflashed partition reads as all-0xFF.
static inline bool esCfgDecode(const uint8_t* buf, size_t len, EsConfig* out) {
    if (!out) return false;
    esCfgDefaults(out);
    if (!buf || len < ESCFG_TOTAL_LEN) return false;

    uint32_t magic;
    memcpy(&magic, buf + 0, 4);
    if (magic != ESCFG_MAGIC) return false;

    uint8_t version = buf[4];
    // A newer version than we understand must be ignored, never reinterpreted:
    // we cannot know what changed, and applying half a config is worse than none.
    if (version == 0 || version > ESCFG_VERSION) return false;

    uint32_t stored;
    memcpy(&stored, buf + ESCFG_CRC_OFFSET, 4);
    if (stored != esCfgCrc32(buf, ESCFG_CRC_OFFSET)) return false;

    uint16_t flags;
    uint32_t engines;
    memcpy(&flags,   buf + 6, 2);
    memcpy(&engines, buf + 8, 4);

    out->flags      = flags;
    out->engines    = engines;
    out->minScore   = buf[12];
    out->rssiFloor  = (int8_t)buf[13];
    out->trackerMin = buf[14];
    esCfgClamp(out);
    return true;
}

// Encode into a caller-provided buffer of at least ESCFG_TOTAL_LEN bytes. Used by
// tests and the JS cross-check; the device never writes its own config, so a
// device-side bug cannot corrupt the user's settings.
static inline void esCfgEncode(const EsConfig* c, uint8_t* buf, size_t len) {
    if (!buf || len < ESCFG_TOTAL_LEN || !c) return;
    memset(buf, 0, ESCFG_TOTAL_LEN);
    uint32_t magic = ESCFG_MAGIC;
    memcpy(buf + 0, &magic, 4);
    buf[4] = ESCFG_VERSION;
    uint16_t flags = c->flags;
    uint32_t engines = c->engines;
    memcpy(buf + 6, &flags, 2);
    memcpy(buf + 8, &engines, 4);
    buf[12] = c->minScore;
    buf[13] = (uint8_t)c->rssiFloor;
    buf[14] = c->trackerMin;
    uint32_t crc = esCfgCrc32(buf, ESCFG_CRC_OFFSET);
    memcpy(buf + ESCFG_CRC_OFFSET, &crc, 4);
}

