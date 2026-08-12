// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2024 SimeonOnSecurity <https://github.com/simeononsecurity>

/*
 * Eye Spy v1.3
 * Target:  M5Stack Atom Lite  (ESP32-PICO-D4, SK6812 LED GPIO27, button GPIO39)
 * Purpose: Passive detector for recording devices, surveillance cameras,
 *          trackers, ALPR, and privacy-threatening wireless equipment
 *
 * ─── BLE Detection Engines (NimBLE passive) ─────────────────────────────────
 *  1.  Axon body camera     — OUI 00:25:df                               → +5 ALERT
 *  2.  Ray-Ban Meta         — svc UUID 0xFD5F                            → +5 ALERT
 *  3.  Flock Safety BLE     — name "Flock","Raven","Penguin","Pigvision",
 *                             "FS Ext Battery"                           → +5 ALERT
 *  4.  Flock BLE mfr ID     — company 0x09C8 (XUNTONG, confirmed Flock) → +5 ALERT
 *  5.  Raven surveillance   — GATT svc UUID 0x3100/0x3200/0x3300/…      → +5 ALERT
 *  6.  Skimmer (HC-03/05/06)— BLE device name exact match               → +5 ALERT
 *  7.  AirTag               — mfr 0x004C subtype 0x12                   → +4 ALERT
 *  8.  OpenDroneID BLE      — svc UUID 0xFFFA / raw AD payload          → +4 ALERT
 *  9.  SmartTag (Samsung)   — svc UUID 0xFD5A                           → +3 CAUTION
 * 10.  Tile tracker         — svc UUID 0xFEED or 0xFEEC                 → +3 CAUTION
 * 11.  MeshCore             — name prefix "MeshCore-"                   → +2 CAUTION
 * 12.  iBeacon              — mfr 0x004C type 0x02 len 0x15             → +2 CAUTION
 * 13.  Persistent unknown   — same MAC seen ≥3× over ≥5 min             → +2 CAUTION
 *
 * ─── WiFi Scan Engines (active) ─────────────────────────────────────────────
 * 14.  Flock Safety OUI     — 35 OUIs, direct + field-verified          → +5 ALERT
 * 15.  Flock keyword SSID   — "flock","raven","penguin","pigvision",…   → +5 ALERT
 * 16.  ALPR OUI             — Motorola/Vigilant LPR cameras             → +5 ALERT
 * 17.  ALPR keyword SSID    — "alpr","lpr","vigilant"…                  → +4 ALERT
 * 18.  SoundThinking OUI    — d4:11:d6 (ShotSpotter, co-deployed)       → +4 ALERT
 * 19.  Camera vendor OUI    — Hikvision/Dahua/Ring/Nest/…               → +3 CAUTION
 * 20.  Camera keyword SSID  — "cam","cctv","dvr","doorbell"…            → +2 CAUTION
 * 21.  Flock mfr-only OUI   — Liteon/USI (shared hardware, low conf.)   → +2 CAUTION
 *
 * ─── WiFi Promiscuous ───────────────────────────────────────────────────────
 * 22.  OpenDroneID NaN      — mgmt dst 51:6f:9a:01:00:00                → +4 ALERT
 *
 * ─── LED ─────────────────────────────────────────────────────────────────────
 *  Blue pulse   = startup
 *  Green solid  = clear   (score 0-2)
 *  Yellow solid = caution (score 3-5)
 *  Red flash    = alert   (score 6+)
 *
 * Score decays −1 per 60 s; 120 s re-score cooldown per device type.
 * Phase: BLE 9 s → WiFi scan ~3 s → Promiscuous 5 s → repeat
 * Serial prefix: [eyespy]
 *
 * Sync notes (v1.2):
 *  • FLOCK_OUIS expanded from 21 → 35 entries (fy_detect.h fy_oui_high[] parity)
 *    Added: b8:35:32, c0:35:32, 24:b2:b9, b8:1e:a4, 70:08:94, 3c:71:bf,
 *           58:00:e3, 5c:93:a2, 64:6e:69, 48:27:ea, a4:cf:12, e0:4f:43,
 *           82:6b:f2 (DeFlockJoplin), b4:1e:52 (Flock direct IEEE)
 *  • New FLOCK_MFR_OUIS — 6 Liteon/USI contract-mfr OUIs (+2 silent)
 *  • New SOUNDTHINKING_OUIS — d4:11:d6 (+4)
 *  • BLE: "Raven" added to name list; mfr-ID 0x09C8 (XUNTONG); Raven UUIDs
 *  • BLE scan: setMaxResults(0) frees results RAM; setDuplicateFilter(false)
 *    ensures every packet is delivered (needed for Raven UUID in scan response)
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <NimBLEDevice.h>
#include <NimBLEScan.h>
#include <NimBLEAdvertisedDevice.h>
#include <string>
#include <cstring>
#include <cctype>

// Core2 For AWS has the same 320×240 ILI9342C display and M5Unified API as Basic.
// Map USE_M5CORE2_AWS → USE_M5BASIC so all existing display guards work.
#if defined(USE_M5CORE2_AWS) && !defined(USE_M5BASIC)
  #define USE_M5BASIC 1
#endif

// ─── Platform feature selection ──────────────────────────────────────────────
#if defined(ATOM_ECHO)
  #define USE_LED         0
  #define USE_BUZZER      1
  #define BUZZER_PIN      25
  #define USE_M5_SPEAKER  0
#elif defined(ATOM_VOICE)
  #define USE_LED         1
  #define USE_BUZZER      0
  #define USE_M5_SPEAKER  1
  #include <M5Unified.h>
#elif defined(T_DONGLE_C5)
  // LILYGO T-Dongle C5 — ESP32-C5 RISC-V, dual-band WiFi 6 + BT5
  // ST7735S TFT (80×160) + WS2812B RGB LED via c5_display.h
  #define USE_LED         0
  #define USE_BUZZER      0
  #define USE_M5_SPEAKER  0
  #define USE_C5_DISPLAY  1
#elif defined(USE_M5BASIC)
  // M5Stack Basic Core v2.7 / Core2 For AWS (aliased above)
  #define USE_LED         0
  #define USE_BUZZER      0
  #define USE_M5_SPEAKER  0
#elif defined(USE_M5STICKC_PLUS_SE)
  // M5StickC Plus SE — passive buzzer G2, two buttons A/B
  // M5Unified used for display (ST7789v2+AXP192); speaker disabled (no NS4168).
  // USE_BUZZER=1 → tone(2,...) for alerts; startup tones via existing USE_BUZZER block.
  #define USE_LED         0
  #define USE_BUZZER      1
  #define BUZZER_PIN      2
  #define USE_M5_SPEAKER  0
#else
  #define USE_LED         1
  #define USE_BUZZER      0
  #define USE_M5_SPEAKER  0
#endif

#include "led_neopixel.h"

// T-Dongle C5 TFT display + RGB LED
#if defined(USE_C5_DISPLAY) && USE_C5_DISPLAY
  #include "c5_display.h"
#endif

// M5Stack Basic Core v2.7 / Core2 For AWS — 320×240 IPS display via M5Unified
#if defined(USE_M5BASIC)
  #include "m5basic_display.h"
#endif

// M5StickC Plus SE — 1.14" ST7789v2 display (240×135 landscape)
#if defined(USE_M5STICKC_PLUS_SE)
  #include "m5stickc_display.h"
#endif

// ─── Hardware ────────────────────────────────────────────────────────────────
// LED_PIN/LED_COUNT are defined inside led_neopixel.h (included above) so
// they live next to the strip object and setLED()/ledBegin() primitives
// that use them.
#define BUTTON_PIN  39

// ── Simple single-GPIO button support (Atom Lite/Echo/Voice, DevKit, T-Dongle C5) ──
// M5Stack Basic/Core2 and M5StickC Plus SE get button handling through
// M5Unified (M5.BtnA/B) inside their respective display headers — see
// m5basicButtonTick()/m5stickcButtonTick() below.  Every other supported
// board (Atom Lite/Echo/Voice, plain ESP32 DevKit, LILYGO T-Dongle C5) has
// only a single bare GPIO button with no M5Unified Button_Class helper.
// BUTTON_PIN/C5_BTN_PIN were previously #defined but never actually read
// anywhere, so the button did nothing on these boards.  This wires up a
// minimal debounced digitalRead() handler.
#if defined(USE_M5BASIC) || defined(USE_M5STICKC_PLUS_SE)
  // handled via M5Unified below — no simple-button wiring needed
#elif defined(T_DONGLE_C5)
  #define HAS_SIMPLE_BUTTON  1
  #define SIMPLE_BUTTON_PIN  C5_BTN_PIN
#else
  #define HAS_SIMPLE_BUTTON  1
  #define SIMPLE_BUTTON_PIN  BUTTON_PIN
#endif

#if defined(HAS_SIMPLE_BUTTON)
static bool          simpleBtnLastState    = true;  // idle = HIGH (pulled up)
static unsigned long simpleBtnLastChangeMs = 0;
#define SIMPLE_BUTTON_DEBOUNCE_MS 50

// Returns true exactly once per physical press (debounced falling edge).
static bool simpleButtonPressed() {
  bool cur = digitalRead(SIMPLE_BUTTON_PIN);
  unsigned long now = millis();
  if (cur != simpleBtnLastState && (now - simpleBtnLastChangeMs) > SIMPLE_BUTTON_DEBOUNCE_MS) {
    simpleBtnLastChangeMs = now;
    simpleBtnLastState    = cur;
    if (!cur) return true;   // LOW == pressed
  }
  return false;
}
#endif


// ─── Tuning ──────────────────────────────────────────────────────────────────
#define RSSI_MIN               -90
#define BLE_SCAN_DURATION_S      9
#define PROMISC_WINDOW_MS     5000UL
#define ALERT_FLASH_HALF_MS    120
#define STARTUP_PULSE_MS       600
#define STARTUP_DURATION_MS   3000UL

// Note: SCORE_DECAY_INTERVAL, DETECTION_RESCORE_MS, SCORE_ALERT,
// SCORE_CAUTION, and all per-engine PTS_* score weights now live in
// es_confidence.h (included further below) alongside the rest of the
// confidence-scoring engine they configure.

// Channel hop list for promiscuous sniff
static const uint8_t CHANNELS[] = {1,6,11,3,8,13};
#define NUM_CHANNELS (sizeof(CHANNELS)/sizeof(CHANNELS[0]))

// ─── Audio alert helper ───────────────────────────────────────────────────────
static void audioAlert(bool isAlert) {
#if USE_BUZZER
    tone(BUZZER_PIN, isAlert ? 2000 : 880, 80);
#elif USE_M5_SPEAKER
    M5.Speaker.tone(isAlert ? 2000 : 880, 80);
#else
    (void)isAlert;
#endif
}

#include "es_detect.h"
#include "es_confidence.h"

// Detection tracking for m5basic_display — always compiled (tiny: ~50 bytes).
// Updated by mbeDetTrack() which is called from CHECK_DET.
static char     g_mbeLastDet[32]  = {0};
static int8_t   g_mbeLastRssi     = -100;
static uint32_t g_mbeTotalEvents  = 0;

// ─── Phase / app state ────────────────────────────────────────────────────────
// PHASE_WIFI_WAIT: async scan started; main loop keeps running (LED never stalls)
enum Phase    { PHASE_BLE, PHASE_WIFI_SCAN, PHASE_WIFI_WAIT, PHASE_PROMISC };
enum AppState { STATE_STARTUP, STATE_NORMAL };
static Phase         g_phase      = PHASE_BLE;
static unsigned long g_phaseStart = 0;
static uint8_t       g_chanIdx    = 0;
static AppState      g_state      = STATE_STARTUP;
static unsigned long g_startupMs  = 0;

// ─── WiFi promiscuous ─────────────────────────────────────────────────────────
static const uint8_t NAN_DEST[6] = {0x51,0x6f,0x9a,0x01,0x00,0x00};

static void IRAM_ATTR wifiSniffer(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (!buf || type != WIFI_PKT_MGMT) return;
    auto* pkt = reinterpret_cast<wifi_promiscuous_pkt_t*>(buf);
    if (pkt->rx_ctrl.rssi < RSSI_MIN) return;
    if (pkt->rx_ctrl.sig_len < 10) return;
    if (memcmp(&pkt->payload[4], NAN_DEST, 6) == 0) {
        g_odidWifiDet  = true;
        g_odidWifiRssi = (int8_t)pkt->rx_ctrl.rssi;
        g_odidWifiSeen = (unsigned long)millis();
    }
}

static void promiscStart() {
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(wifiSniffer);
    esp_wifi_set_promiscuous(true);
    Serial.println("[eyespy] promisc ON");
}
static void promiscStop() {
    esp_wifi_set_promiscuous(false);
    Serial.println("[eyespy] promisc OFF");
}
static void channelHop() {
    static unsigned long lastHop = 0;
    unsigned long now = millis();
    if (now - lastHop >= 400) {
        lastHop = now;
        g_chanIdx = (g_chanIdx + 1) % NUM_CHANNELS;
        esp_wifi_set_channel(CHANNELS[g_chanIdx], WIFI_SECOND_CHAN_NONE);
    }
}

// ─── BLE callback ─────────────────────────────────────────────────────────────
// strContainsCI() now lives in es_detect.h alongside the pattern tables it
// matches against.
static NimBLEScan* g_pScan = nullptr;

class EyeSpyBLECallbacks : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* adv) override {
        int8_t rssi = adv->getRSSI();
        if (rssi < RSSI_MIN) return;

        unsigned long now  = (unsigned long)millis();
        std::string   addr = adv->getAddress().toString();
        std::string   name = adv->getName();
        bool          matched = false;

        // ── 1. Axon body camera — OUI 00:25:df ──────────────────────────────
        if (addr.size() >= 8 &&
            addr[0]=='0' && addr[1]=='0' && addr[2]==':' &&
            addr[3]=='2' && addr[4]=='5' && addr[5]==':' &&
            tolower((unsigned char)addr[6])=='d' &&
            tolower((unsigned char)addr[7])=='f') {
            g_axonDet=true; g_axonRssi=rssi; g_axonSeen=now; matched=true;
        }

        // ── 2. Ray-Ban Meta — svc UUID 0xFD5F ───────────────────────────────
        if (adv->isAdvertisingService(NimBLEUUID((uint16_t)0xFD5F))) {
            g_raybanDet=true; g_raybanRssi=rssi; g_raybanSeen=now; matched=true;
        }

        // ── 3. Flock Safety BLE — device name (Flock/Raven/Penguin/Pigvision/FS Ext Battery)
        if (!name.empty()) {
            for (const char** kw = FLOCK_BLE_NAMES; *kw; kw++) {
                if (strContainsCI(name.c_str(), *kw)) {
                    g_flockBleDet=true; g_flockBleRssi=rssi; g_flockBleSeen=now;
                    matched=true; break;
                }
            }
        }

        // ── 4. Flock BLE mfr-ID — 0x09C8 (XUNTONG Technology, confirmed Flock)
        //    Source: wgreenberg/flock-you field capture.
        {
            std::string mfrData = adv->getManufacturerData();
            if (mfrData.size() >= 2) {
                const uint8_t* m = (const uint8_t*)mfrData.data();
                // BLE mfr data is little-endian company ID: 0x09C8 → bytes C8 09
                uint16_t cid = (uint16_t)(m[0] | (m[1] << 8));
                if (cid == 0x09C8) {
                    g_flockBleMfrDet=true; g_flockBleMfrRssi=rssi; g_flockBleMfrSeen=now;
                    matched=true;
                }
                // AirTag / iBeacon — Apple 0x004C (bytes 4C 00)
                if (m[0]==0x4C && m[1]==0x00 && mfrData.size() >= 3) {
                    uint8_t subtype = m[2];
                    if (subtype == 0x12 || subtype == 0x1E) {  // FindMy / AirTag
                        g_airtagDet=true; g_airtagRssi=rssi; g_airtagSeen=now; matched=true;
                    }
                    if (subtype == 0x02 && mfrData.size() >= 4 && m[3] == 0x15) {  // iBeacon
                        g_ibeaconDet=true; g_ibeaconRssi=rssi; g_ibeaconSeen=now; matched=true;
                    }
                }
            }
        }

        // Raw payload AirTag fallback
        {
            const uint8_t* p    = adv->getPayload();
            size_t         plen = adv->getPayloadLength();
            if (p && plen >= 4) {
                for (size_t i = 0; i + 3 < plen; i++) {
                    if ((p[i]==0x1E && p[i+1]==0xFF && p[i+2]==0x4C && p[i+3]==0x00) ||
                        (p[i]==0x4C && p[i+1]==0x00 && p[i+2]==0x12)) {
                        g_airtagDet=true; g_airtagRssi=rssi; g_airtagSeen=now;
                        matched=true; break;
                    }
                }
            }
        }

        // ── 5. Raven surveillance device — GATT service UUIDs (GainSec research)
        //    Check 128-bit service UUIDs in the advertisement.
        //    Uses NimBLE getServiceUUID() iteration.
        if (!g_ravenBleDet) {
            for (const char** uuid = RAVEN_UUIDS; *uuid; uuid++) {
                if (adv->isAdvertisingService(NimBLEUUID(*uuid))) {
                    g_ravenBleDet=true; g_ravenBleRssi=rssi; g_ravenBleSeen=now;
                    // Rate-limit the UUID detail log; always print on first detection
                    if (g_ravenBleCount == 0 || now - g_ravenBleLoggedAt >= DETECTION_RESCORE_MS) {
                        Serial.printf("[eyespy] Raven UUID %s RSSI=%d\n", *uuid, (int)rssi);
                    }
                    matched=true; break;
                }
            }
        }

        // ── 6. Card skimmer — name exact match HC-03/05/06 ───────────────────
        if (!name.empty()) {
            for (const char** kw = SKIMMER_NAMES; *kw; kw++) {
                if (name == *kw) {
                    g_skimmerDet=true; g_skimmerRssi=rssi; g_skimmerSeen=now;
                    matched=true; break;
                }
            }
        }

        // ── 7/8. AirTag raw payload check already done above in block 4 ─────

        // ── 9. OpenDroneID — svc UUID 0xFFFA ────────────────────────────────
        if (adv->isAdvertisingService(NimBLEUUID((uint16_t)0xFFFA))) {
            g_odidBleDet=true; g_odidBleRssi=rssi; g_odidBleSeen=now; matched=true;
        } else {
            const uint8_t* p    = adv->getPayload();
            size_t         plen = adv->getPayloadLength();
            size_t         i    = 0;
            while (p && i + 1 < plen) {
                uint8_t elen = p[i]; if (!elen) break;
                if (i+elen < plen && elen >= 4 && p[i+1]==0x16 &&
                    p[i+2]==0xFA && p[i+3]==0xFF &&
                    (elen < 5 || p[i+4]==0x0D)) {
                    g_odidBleDet=true; g_odidBleRssi=rssi; g_odidBleSeen=now;
                    matched=true; break;
                }
                i += (size_t)(elen + 1);
            }
        }

        // ── 10. Samsung SmartTag — svc UUID 0xFD5A ───────────────────────────
        if (adv->isAdvertisingService(NimBLEUUID((uint16_t)0xFD5A))) {
            g_smarttagDet=true; g_smarttagRssi=rssi; g_smarttagSeen=now; matched=true;
        }

        // ── 11. Tile tracker — svc UUID 0xFEED or 0xFEEC ─────────────────────
        if (adv->isAdvertisingService(NimBLEUUID((uint16_t)0xFEED)) ||
            adv->isAdvertisingService(NimBLEUUID((uint16_t)0xFEEC))) {
            g_tileDet=true; g_tileRssi=rssi; g_tileSeen=now; matched=true;
        }

        // ── 12. MeshCore — name prefix "MeshCore-" ────────────────────────────
        if (!name.empty() && name.size() >= 9 &&
            name.compare(0, 9, "MeshCore-") == 0) {
            g_meshcoreDet=true; g_meshcoreRssi=rssi; g_meshcoreSeen=now; matched=true;
        }

        // ── 13. Device persistence tracker (unknown devices) ─────────────────
        if (!matched && addr.size() == 17) {
            bool found = false;
            for (uint8_t i = 0; i < g_trackedCount; i++) {
                if (strcmp(g_tracked[i].addr, addr.c_str()) == 0) {
                    g_tracked[i].lastSeen = now;
                    g_tracked[i].seenCount++;
                    found = true;
                    if (!g_tracked[i].scored &&
                        g_tracked[i].seenCount >= PERSIST_MIN_COUNT &&
                        (now - g_tracked[i].firstSeen) >= PERSIST_MIN_MS) {
                        g_tracked[i].scored = true;
                        g_persistDet  = true;
                        g_persistRssi = rssi;
                        g_persistSeen = now;
                    }
                    break;
                }
            }
            if (!found && g_trackedCount < MAX_TRACKED) {
                TrackedDev& d = g_tracked[g_trackedCount++];
                strncpy(d.addr, addr.c_str(), 17); d.addr[17]='\0';
                d.firstSeen = now; d.lastSeen = now;
                d.seenCount = 1; d.scored = false;
            }
        }
    }
};

static EyeSpyBLECallbacks g_bleCallbacks;

static void startBLEScan() {
    if (!g_pScan || g_pScan->isScanning()) return;
    g_pScan->clearResults();
    g_pScan->start(BLE_SCAN_DURATION_S, false);
    Serial.println("[eyespy] BLE scan start");
}

// ─── Detection tracking helper ────────────────────────────────────────────────
// ouiMatch()/ssidHas() now live in es_detect.h alongside the pattern tables
// they match against.
// Records last-fired detection type/RSSI for m5basic_display.
// Compiled unconditionally (tiny); called from CHECK_DET.
static inline void mbeDetTrack(const char* tag, int8_t rssi) {
    strncpy(g_mbeLastDet, tag, 31); g_mbeLastDet[31] = '\0';
    g_mbeLastRssi = rssi;
    g_mbeTotalEvents++;
}

// Mirrors a detection-engine log line onto the M5Basic/Core2 on-screen log
// strip (no-op on boards without USE_M5BASIC).  Kept as a macro (not a
// function) so it can be used inside the CHECK_DET() macro below, which is
// itself expanded inside processBLE() — a plain function call would work
// too, but the macro form avoids an extra forward declaration here.
#if defined(USE_M5BASIC)
  #define IF_M5BASIC_LOG(tag, rssi, cnt) do { \
      char _mbeLine[64]; \
      snprintf(_mbeLine, sizeof(_mbeLine), tag "  RSSI=%d  #%u", (int)(rssi), (unsigned)(cnt)); \
      mbe_logAdd(_mbeLine); \
  } while (0)
#else
  #define IF_M5BASIC_LOG(tag, rssi, cnt) do {} while (0)
#endif

// addScore() now lives in es_confidence.h alongside the rest of the scoring engine.

// ─── WiFi scan ───────────────────────────────────────────────────────────────
// Called once the async scan has completed with result count n.
static void processWifiScan(int n) {
    if (n <= 0) { Serial.println("[eyespy] scan 0 nets"); return; }

    unsigned long now = millis();
    bool fFlockOui=false, fFlockMfrOui=false, fSoundthinking=false,
         fAlprOui=false,  fFlockSsid=false,   fAlprSsid=false,
         fCamOui=false,   fCamSsid=false;

    for (int i = 0; i < n; i++) {
        if (WiFi.RSSI(i) < RSSI_MIN) continue;
        uint8_t*    bssid = WiFi.BSSID(i);
        const char* ssid  = WiFi.SSID(i).c_str();

        // High-confidence Flock Safety OUI
        if (!fFlockOui && bssid && ouiMatch(bssid, FLOCK_OUIS, NUM_FLOCK_OUIS)) {
            Serial.printf("[eyespy] Flock-cam OUI %02x:%02x:%02x \"%s\"\n",
                          bssid[0],bssid[1],bssid[2], ssid);
            fFlockOui = true;
        }
        // Contract-mfr OUI (Liteon/USI — low confidence)
        if (!fFlockMfrOui && bssid && ouiMatch(bssid, FLOCK_MFR_OUIS, NUM_FLOCK_MFR_OUIS)) {
            Serial.printf("[eyespy] Flock-mfr OUI %02x:%02x:%02x \"%s\"\n",
                          bssid[0],bssid[1],bssid[2], ssid);
            fFlockMfrOui = true;
        }
        // SoundThinking/ShotSpotter acoustic sensor
        if (!fSoundthinking && bssid && ouiMatch(bssid, SOUNDTHINKING_OUIS, NUM_SOUNDTHINKING_OUIS)) {
            Serial.printf("[eyespy] SoundThinking OUI %02x:%02x:%02x \"%s\"\n",
                          bssid[0],bssid[1],bssid[2], ssid);
            fSoundthinking = true;
        }
        // Motorola / Vigilant LPR camera OUI
        if (!fAlprOui && bssid && ouiMatch(bssid, ALPR_OUIS, NUM_ALPR_OUIS)) {
            Serial.printf("[eyespy] ALPR OUI      %02x:%02x:%02x \"%s\"\n",
                          bssid[0],bssid[1],bssid[2], ssid);
            fAlprOui = true;
        }
        if (!fFlockSsid && ssidHas(ssid, FLOCK_SSID_KW)) {
            Serial.printf("[eyespy] Flock SSID \"%s\"\n", ssid);
            fFlockSsid = true;
        }
        if (!fAlprSsid && ssidHas(ssid, ALPR_SSID_KW)) {
            Serial.printf("[eyespy] ALPR SSID \"%s\"\n", ssid);
            fAlprSsid = true;
        }
        if (!fCamOui && bssid && ouiMatch(bssid, CAM_OUIS, NUM_CAM_OUIS)) {
            Serial.printf("[eyespy] cam OUI %02x:%02x:%02x \"%s\"\n",
                          bssid[0],bssid[1],bssid[2], ssid);
            fCamOui = true;
        }
        if (!fCamSsid && ssidHas(ssid, CAM_SSID_KW)) {
            Serial.printf("[eyespy] cam SSID \"%s\"\n", ssid);
            fCamSsid = true;
        }
    }
    WiFi.scanDelete();

    if (fFlockOui)     addScore(PTS_FLOCK_OUI,     now, &g_flockOuiScored,     "Flock-cam-OUI");
    if (fFlockMfrOui)  addScore(PTS_FLOCK_MFR_OUI, now, &g_flockMfrOuiScored,  "Flock-mfr-OUI");
    if (fSoundthinking)addScore(PTS_SOUNDTHINKING,  now, &g_soundthinkingScored,"SoundThinking");
    if (fAlprOui)      addScore(PTS_ALPR_OUI,       now, &g_alprOuiScored,      "ALPR-OUI");
    if (fFlockSsid)    addScore(PTS_FLOCK_SSID,     now, &g_flockSsidScored,    "Flock-SSID");
    if (fAlprSsid)     addScore(PTS_ALPR_SSID,      now, &g_alprSsidScored,     "ALPR-SSID");
    if (fCamOui)       addScore(PTS_CAM_OUI,        now, &g_camOuiScored,       "cam-OUI");
    if (fCamSsid)      addScore(PTS_CAM_SSID,       now, &g_camSsidScored,      "cam-SSID");
    Serial.printf("[eyespy] WiFi done  score=%d\n", g_score);
}

static void startWifiPromisc() {
    promiscStart();
    g_chanIdx = 0;
    esp_wifi_set_channel(CHANNELS[0], WIFI_SECOND_CHAN_NONE);
    g_phase = PHASE_PROMISC; g_phaseStart = millis();
}

// ─── Process BLE detections ───────────────────────────────────────────────────
// CHECK_DET(...) is defined in es_confidence.h (included above) alongside
// the rest of the scoring engine it drives.
static void processBLE() {
    unsigned long now = millis();

    CHECK_DET(axon,        PTS_AXON,         "Axon-cam");
    CHECK_DET(rayban,      PTS_RAYBAN,       "RayBan-Meta");
    CHECK_DET(flockBle,    PTS_FLOCK_BLE,    "Flock-BLE-name");
    CHECK_DET(flockBleMfr, PTS_FLOCK_BLE_MFR,"Flock-BLE-mfrID");
    CHECK_DET(ravenBle,    PTS_RAVEN_BLE,    "Raven-BLE-UUID");
    CHECK_DET(skimmer,     PTS_SKIMMER,      "Skimmer");
    CHECK_DET(airtag,      PTS_AIRTAG,       "AirTag");
    CHECK_DET(odidBle,     PTS_ODID_BLE,     "ODID-BLE");
    CHECK_DET(odidWifi,    PTS_ODID_WIFI,    "ODID-WiFi");
    CHECK_DET(smarttag,    PTS_SMARTTAG,     "SmartTag");
    CHECK_DET(tile,        PTS_TILE,         "Tile");
    CHECK_DET(meshcore,    PTS_MESHCORE,     "MeshCore");
    CHECK_DET(ibeacon,     PTS_IBEACON,      "iBeacon");
    CHECK_DET(persist,     PTS_PERSIST,      "PersistTracker");
}

// tickDecay() now lives in es_confidence.h alongside the rest of the scoring engine.

// ─── LED + audio update ───────────────────────────────────────────────────────
static void updateLED() {
    unsigned long now = millis();
    static int    prevLevel = 0;

    if (g_state == STATE_STARTUP) {
        bool on = ((now / STARTUP_PULSE_MS) & 1) == 0;
        setLED(0, 0, on ? 40 : 0); return;
    }

    int level = (g_score >= SCORE_ALERT) ? 2 :
                (g_score >= SCORE_CAUTION) ? 1 : 0;

    if (level > prevLevel) audioAlert(level >= 2);
    prevLevel = level;

    if (level >= 2) {
        bool on = ((now / ALERT_FLASH_HALF_MS) & 1) == 0;
        setLED(on ? 220 : 0, 0, 0);
    } else if (level == 1) {
        setLED(180, 60, 0);
    } else {
        setLED(0, 80, 0);
    }
#if defined(USE_C5_DISPLAY) && USE_C5_DISPLAY
    {
        const char* ph = (g_phase==PHASE_BLE) ? "BLE" :
                         (g_phase==PHASE_WIFI_SCAN) ? "WIFI" : "PROMISC";
        c5DisplayScore(g_score, g_mbeLastDet[0] ? g_mbeLastDet : nullptr, ph, g_mbeLastRssi);
    }
#endif
#if defined(USE_M5BASIC)
    {
        const char* ph2 = (g_phase==PHASE_BLE) ? "BLE" :
                          (g_phase==PHASE_WIFI_SCAN || g_phase==PHASE_WIFI_WAIT) ? "WIFI" : "PROMISC";
        m5basicUpdate(g_score,
                      g_mbeLastDet[0] ? g_mbeLastDet : nullptr,
                      g_mbeLastRssi, ph2,
                      g_stickySeen ? millis() - g_stickySeen : 0UL,
                      (int)g_trackedCount, g_mbeTotalEvents);
    }
#endif
#if defined(USE_M5STICKC_PLUS_SE)
    {
        const char* ph3 = (g_phase==PHASE_BLE) ? "BLE" :
                          (g_phase==PHASE_WIFI_SCAN || g_phase==PHASE_WIFI_WAIT) ? "WIFI" : "PROMISC";
        m5stickcUpdate(g_score,
                       g_mbeLastDet[0] ? g_mbeLastDet : nullptr,
                       g_mbeLastRssi, ph3,
                       g_stickySeen ? millis() - g_stickySeen : 0UL,
                       (int)g_trackedCount, g_mbeTotalEvents);
    }
#endif
}

// ─── Status print ─────────────────────────────────────────────────────────────
static unsigned long g_lastStatus = 0;
static void printStatus() {
    unsigned long now = millis();
    if (now - g_lastStatus < 10000) return;
    g_lastStatus = now;
    const char* ph = (g_phase==PHASE_BLE) ? "BLE" :
                     (g_phase==PHASE_WIFI_SCAN || g_phase==PHASE_WIFI_WAIT) ? "WIFI" : "PROMISC";
    const char* st = (g_score>=SCORE_ALERT) ? "ALERT" :
                     (g_score>=SCORE_CAUTION) ? "CAUTION" : "CLEAR";
    Serial.printf("[eyespy] status  score=%d  %s  phase=%s  tracked=%d\n",
                  g_score, st, ph, (int)g_trackedCount);
#if defined(USE_M5BASIC)
    {
        char _mbeStatusLine[64];
        snprintf(_mbeStatusLine, sizeof(_mbeStatusLine),
                 "status score=%d %s phase=%s tracked=%d",
                 g_score, st, ph, (int)g_trackedCount);
        mbe_logAdd(_mbeStatusLine);
    }
#endif
}


// purgeTracked() now lives in es_confidence.h alongside the persistence
// tracker (TrackedDev/g_tracked/g_trackedCount) it operates on.

// ─── setup() ─────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("[eyespy] Eye Spy v1.3 starting");

#if defined(USE_C5_DISPLAY) && USE_C5_DISPLAY
    c5DisplayInit();
#endif
#if defined(USE_M5BASIC)
    // M5Unified init (display splash, speaker/touch, buttons). Must precede NimBLE and WiFi.
    m5basicInit();
    Serial.println("[eyespy] M5Stack Basic/Core2 ready");
    // Immediately replace the static splash with the live scanning screen so the
    // display doesn't appear stuck on "Init..." while NimBLE/WiFi come up.
    m5basicUpdate(0, nullptr, -100, "BLE", 0UL, 0, 0);
#endif
#if defined(USE_M5STICKC_PLUS_SE)
    // M5Unified init for display (AXP192 backlight) + button detection.
    // Speaker disabled — passive buzzer G2 driven by tone() after this.
    m5stickcInit();
    Serial.println("[eyespy] M5StickC Plus SE ready");
    // Immediately replace the static splash with the live scanning screen so the
    // display doesn't appear stuck on "Init..." while NimBLE/WiFi come up.
    m5stickcUpdate(0, nullptr, -100, "BLE", 0UL, 0, 0);
#endif


    ledBegin(80);
    setLED(0, 0, 40);
    pinMode(BUTTON_PIN, INPUT_PULLUP);

#if defined(HAS_SIMPLE_BUTTON)
    // Single bare-GPIO button (Atom Lite/Echo/Voice, DevKit, T-Dongle C5).
    // Previously #defined but never read anywhere — wire it up now.
    pinMode(SIMPLE_BUTTON_PIN, INPUT_PULLUP);
    simpleBtnLastState = digitalRead(SIMPLE_BUTTON_PIN);
#endif


#if USE_BUZZER
    pinMode(BUZZER_PIN, OUTPUT);
    tone(BUZZER_PIN, 880,  100); delay(150);
    tone(BUZZER_PIN, 1320, 100); delay(150);
    tone(BUZZER_PIN, 1760, 100); delay(200);
    noTone(BUZZER_PIN);
    Serial.println("[eyespy] Atom Echo (buzzer) ready");
#elif USE_M5_SPEAKER
    {
        auto m5cfg = M5.config();
        M5.begin(m5cfg);
        auto spk_cfg = M5.Speaker.config();
        spk_cfg.pin_data_out = 22;
        spk_cfg.pin_bck      = 19;
        spk_cfg.pin_ws       = 33;
        M5.Speaker.config(spk_cfg);
        M5.Speaker.begin();
        M5.Speaker.setVolume(220);
    }
    M5.Speaker.tone(880,  100); delay(150);
    M5.Speaker.tone(1320, 100); delay(150);
    M5.Speaker.tone(1760, 100); delay(200);
    Serial.println("[eyespy] Atom Voice S3R (I2S) ready");
#endif

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    delay(100);

    NimBLEDevice::init("");
    NimBLEDevice::setPower(ESP_PWR_LVL_P3);
    g_pScan = NimBLEDevice::getScan();

    // wantDuplicates=false: don't re-invoke callback for the same MAC per scan
    // session (saves CPU); we'll see a new one on the next scan cycle.
    g_pScan->setAdvertisedDeviceCallbacks(&g_bleCallbacks, /*wantDuplicates=*/false);

    // Passive scan — do not send scan-request PDUs (avoids revealing our presence).
    g_pScan->setActiveScan(false);

    // ~100 ms window / ~100 ms interval = 100% duty cycle for maximum sensitivity.
    // BLE advertising intervals are typically 100–1000 ms, so 9 s × 100% gives
    // ~9 scans per advertising interval for near-100% detection probability.
    g_pScan->setInterval(100);
    g_pScan->setWindow(99);

    // Disable NimBLE's internal results accumulator — we handle everything in
    // the callback.  setMaxResults(0) prevents the scan result list from growing
    // unboundedly and wasting heap on long-running scans.
    g_pScan->setMaxResults(0);

    // Keep Bluetooth duplicate filter OFF so every advertisement packet is
    // delivered to the callback.  Required for Raven UUID detection: some
    // devices only include the service UUID in the scan response (second packet),
    // which would be dropped by the BT controller's duplicate filter.
    g_pScan->setDuplicateFilter(false);

    memset(g_tracked, 0, sizeof(g_tracked));
    g_trackedCount = 0;

    g_startupMs  = millis();
    g_phaseStart = millis();
    g_lastDecay  = millis();
    g_lastStatus = millis();
    g_state      = STATE_STARTUP;

    startBLEScan();
    Serial.printf("[eyespy] init OK  flock_ouis=%u  mfr_ouis=%u  st_ouis=%u\n",
                  (unsigned)NUM_FLOCK_OUIS,
                  (unsigned)NUM_FLOCK_MFR_OUIS,
                  (unsigned)NUM_SOUNDTHINKING_OUIS);
}

// ─── loop() ──────────────────────────────────────────────────────────────────
void loop() {
    unsigned long now = millis();

    if (g_state == STATE_STARTUP && now - g_startupMs >= STARTUP_DURATION_MS) {
        g_state = STATE_NORMAL;
        Serial.println("[eyespy] startup done");
    }

    switch (g_phase) {
        case PHASE_BLE:
            processBLE();
            if (now - g_phaseStart >= (unsigned long)(BLE_SCAN_DURATION_S*1000 + 1000)) {
                if (g_pScan && g_pScan->isScanning()) g_pScan->stop();
                delay(30);
                WiFi.mode(WIFI_STA); WiFi.disconnect(true); delay(50);
                g_phase = PHASE_WIFI_SCAN; g_phaseStart = millis();
            } else {
                if (g_pScan && !g_pScan->isScanning()) startBLEScan();
            }
            break;

        case PHASE_WIFI_SCAN:
            // Kick off an async scan — returns immediately so the loop keeps
            // running and updateLED() is never starved during the scan wait.
            Serial.println("[eyespy] WiFi scan");
            WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/true,
                              /*passive=*/false, /*max_ms_per_chan=*/120);
            g_phase = PHASE_WIFI_WAIT; g_phaseStart = millis();
            break;

        case PHASE_WIFI_WAIT: {
            int wn = WiFi.scanComplete();
            if (wn == WIFI_SCAN_RUNNING) {
                // Still scanning — LED keeps blinking normally.
                // Guard against stale scans (shouldn't happen, but be safe).
                if (now - g_phaseStart > 8000UL) {
                    Serial.println("[eyespy] WiFi scan timeout");
                    WiFi.scanDelete();
                    Serial.printf("[eyespy] WiFi done  score=%d\n", g_score);
                    startWifiPromisc();
                }
                break;
            }
            // Scan finished (success or failed).
            if (wn >= 0) processWifiScan(wn);
            else { Serial.println("[eyespy] WiFi scan failed"); WiFi.scanDelete(); }
            startWifiPromisc();
            break;
        }

        case PHASE_PROMISC:
            channelHop();
            processBLE();
            if (now - g_phaseStart >= PROMISC_WINDOW_MS) {
                promiscStop(); delay(30);
                WiFi.disconnect(true); delay(50);
                g_phase = PHASE_BLE; g_phaseStart = millis();
                startBLEScan();
            }
            break;
    }

    tickDecay();
    updateLED();
    printStatus();
    purgeTracked();
#if defined(USE_M5BASIC)
    {
        int btn = m5basicButtonTick();
        if (btn == 1) {
            g_score = 0; g_stickySeen = 0;
            memset(g_mbeLastDet, 0, sizeof(g_mbeLastDet)); g_mbeLastRssi = -100;
            Serial.println("[eyespy] Score reset (Btn A)");
        } else if (btn == 3) {
            if (g_pScan && g_pScan->isScanning()) g_pScan->stop();
            promiscStop(); WiFi.disconnect(true); delay(50);
            g_phase = PHASE_WIFI_SCAN; g_phaseStart = millis();
            Serial.println("[eyespy] Forced scan (Btn C)");
        }
    }
#endif
#if defined(USE_M5CORE2_AWS)
    m5basicVibrationTick();
#endif
#if defined(USE_M5STICKC_PLUS_SE)

    {
        int btn = m5stickcButtonTick();
        if (btn == 1) {
            g_score = 0; g_stickySeen = 0;
            memset(g_mbeLastDet, 0, sizeof(g_mbeLastDet)); g_mbeLastRssi = -100;
            Serial.println("[eyespy] Score reset (Btn A)");
        } else if (btn == 3) {
            if (g_pScan && g_pScan->isScanning()) g_pScan->stop();
            promiscStop(); WiFi.disconnect(true); delay(50);
            g_phase = PHASE_WIFI_SCAN; g_phaseStart = millis();
            Serial.println("[eyespy] Forced scan (Btn B)");
        }
    }
#endif

#if defined(HAS_SIMPLE_BUTTON)
    // Single bare-GPIO button (Atom Lite/Echo/Voice, DevKit, T-Dongle C5).
    // Press = reset score to 0 + force an immediate scan-cycle restart
    // (mirrors the M5Basic Btn A + Btn C combined action) plus a short
    // audible/visual acknowledgement.
    if (simpleButtonPressed()) {
        g_score = 0; g_stickySeen = 0;
        memset(g_mbeLastDet, 0, sizeof(g_mbeLastDet)); g_mbeLastRssi = -100;
        if (g_pScan && g_pScan->isScanning()) g_pScan->stop();
        promiscStop(); WiFi.disconnect(true); delay(50);
        g_phase = PHASE_WIFI_SCAN; g_phaseStart = millis();
        Serial.println("[eyespy] Score reset + forced scan (button)");
        audioAlert(false);
#if USE_LED
        setLED(0, 0, 255); delay(80); setLED(0, 80, 0);
#endif
    }
#endif

    delay(10);
}

