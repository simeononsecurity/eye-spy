/*
 * Eye Spy v1.1
 * Target:  M5Stack Atom Lite  (ESP32-PICO-D4, SK6812 LED GPIO27, button GPIO39)
 * Purpose: Passive detector for recording devices, surveillance cameras,
 *          trackers, ALPR, and privacy-threatening wireless equipment
 *
 * ─── BLE Detection Engines (NimBLE passive) ─────────────────────────────────
 *  1.  Axon body camera     — OUI 00:25:df                     → +5 ALERT
 *  2.  Ray-Ban Meta         — svc UUID 0xFD5F                  → +5 ALERT
 *  3.  Flock Safety BLE     — name "Flock","Penguin","Pigvision","FS Ext Battery"→ +5 ALERT
 *  4.  Skimmer (HC-03/05/06)— BLE device name exact match      → +5 ALERT
 *  5.  AirTag               — mfr 0x004C subtype 0x12          → +4 ALERT
 *  6.  OpenDroneID BLE      — svc UUID 0xFFFA / raw AD payload → +4 ALERT
 *  7.  SmartTag (Samsung)   — svc UUID 0xFD5A                  → +3 CAUTION
 *  8.  Tile tracker         — svc UUID 0xFEED or 0xFEEC        → +3 CAUTION
 *  9.  MeshCore             — name prefix "MeshCore-"          → +2 CAUTION
 * 10.  iBeacon              — mfr 0x004C type 0x02 len 0x15    → +2 CAUTION
 * 11.  Persistent unknown   — same MAC seen ≥3× over ≥5 min    → +2 CAUTION
 *
 * ─── WiFi Scan Engines (active) ─────────────────────────────────────────────
 * 12.  ALPR / Flock OUI     — Motorola/Vigilant/Flock BSSIDs   → +5 ALERT
 * 13.  Flock keyword SSID   — "flock","penguin","pigvision"…    → +5 ALERT
 * 14.  ALPR keyword SSID    — "alpr","lpr","vigilant"…         → +4 ALERT
 * 15.  Camera vendor OUI    — Hikvision/Dahua/Ring/Nest/…      → +3 CAUTION
 * 16.  Camera keyword SSID  — "cam","cctv","dvr","doorbell"…   → +2 CAUTION
 *
 * ─── WiFi Promiscuous ───────────────────────────────────────────────────────
 * 17.  OpenDroneID NaN      — mgmt dst 51:6f:9a:01:00:00       → +4 ALERT
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
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <Adafruit_NeoPixel.h>
#include <NimBLEDevice.h>
#include <NimBLEScan.h>
#include <NimBLEAdvertisedDevice.h>
#include <string>
#include <cstring>
#include <cctype>

// ─── Hardware ────────────────────────────────────────────────────────────────
#ifdef DEVKIT_LED
  #define LED_PIN   2
#else
  #define LED_PIN   27
#endif
#define LED_COUNT   1
#define BUTTON_PIN  39

// ─── Tuning ──────────────────────────────────────────────────────────────────
#define RSSI_MIN               -90
#define BLE_SCAN_DURATION_S      9
#define PROMISC_WINDOW_MS     5000UL
#define SCORE_DECAY_INTERVAL  60000UL
#define DETECTION_RESCORE_MS 120000UL
#define SCORE_ALERT            6
#define SCORE_CAUTION          3
#define ALERT_FLASH_HALF_MS    120
#define STARTUP_PULSE_MS       600
#define STARTUP_DURATION_MS   3000UL

// Scores
#define PTS_AXON               5
#define PTS_RAYBAN             5
#define PTS_FLOCK_BLE          5
#define PTS_SKIMMER            5
#define PTS_AIRTAG             4
#define PTS_ODID_BLE           4
#define PTS_ODID_WIFI          4
#define PTS_SMARTTAG           3
#define PTS_TILE               3
#define PTS_MESHCORE           2
#define PTS_IBEACON            2
#define PTS_PERSIST            2
#define PTS_ALPR_OUI           5
#define PTS_FLOCK_SSID         5
#define PTS_ALPR_SSID          4
#define PTS_CAM_OUI            3
#define PTS_CAM_SSID           2

// Channel hop list for promiscuous sniff
static const uint8_t CHANNELS[] = {1,6,11,3,8,13};
#define NUM_CHANNELS (sizeof(CHANNELS)/sizeof(CHANNELS[0]))

// ─── LED ─────────────────────────────────────────────────────────────────────
static Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
inline void setLED(uint8_t r, uint8_t g, uint8_t b) {
    strip.setPixelColor(0, strip.Color(r, g, b));
    strip.show();
}

// ─── OUI / SSID tables ───────────────────────────────────────────────────────
// Camera vendor OUIs
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

// ALPR / Flock Safety OUIs (combined — all score ALPR)
static const uint8_t ALPR_OUIS[][3] = {
    {0x00,0x0e,0x58},                                   // Motorola/Vigilant
    {0xd4,0xbb,0xe6},{0x3c,0x61,0x05},                  // Flock Safety (known)
    // Flock/FS-Ext-Battery MAC prefixes from nyanBOX flock_detector
    {0x58,0x8e,0x81},{0xec,0x1b,0xbd},{0x90,0x35,0xea},
    {0x04,0x0d,0x84},{0xf0,0x82,0xc0},{0x1c,0x34,0xf1},
    {0x38,0x5b,0x44},{0x94,0x34,0x69},{0xb4,0xe3,0xf9},
    {0x70,0xc9,0x4e},{0x3c,0x91,0x80},{0xd8,0xf3,0xbc},
    {0x80,0x30,0x49},{0x14,0x5a,0xfc},{0x74,0x4c,0xa1},
    {0x08,0x3a,0x88},{0x9c,0x2f,0x9d},{0x94,0x08,0x53},
    {0xe4,0xaa,0xea},
};
#define NUM_ALPR_OUIS (sizeof(ALPR_OUIS)/sizeof(ALPR_OUIS[0]))

// Flock-specific SSID keywords (score +5)
static const char* FLOCK_SSID_KW[] = {
    "flock", "flocksafety", "fs ext", "penguin", "pigvision", nullptr
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

// Flock BLE device name patterns
static const char* FLOCK_BLE_NAMES[] = {
    "flock", "penguin", "pigvision", "fs ext battery", nullptr
};

// HC-0x card skimmer names (exact match)
static const char* SKIMMER_NAMES[] = { "HC-03", "HC-05", "HC-06", nullptr };

// ─── Detection state for each engine ─────────────────────────────────────────
// Each uses: volatile bool flag, volatile int8_t rssi, volatile ul lastSeen, ul lastScored
#define DECL_DETECTOR(name) \
    static volatile bool     g_##name##Det   = false; \
    static volatile int8_t   g_##name##Rssi  = -100;  \
    static volatile unsigned long g_##name##Seen = 0;  \
    static          unsigned long g_##name##Scored = 0

DECL_DETECTOR(axon);
DECL_DETECTOR(rayban);
DECL_DETECTOR(flockBle);
DECL_DETECTOR(skimmer);
DECL_DETECTOR(airtag);
DECL_DETECTOR(odidBle);
DECL_DETECTOR(odidWifi);
DECL_DETECTOR(smarttag);
DECL_DETECTOR(tile);
DECL_DETECTOR(meshcore);
DECL_DETECTOR(ibeacon);
DECL_DETECTOR(persist);

// WiFi scored inline (not volatile)
static unsigned long g_alprOuiScored  = 0;
static unsigned long g_flockSsidScored= 0;
static unsigned long g_alprSsidScored = 0;
static unsigned long g_camOuiScored   = 0;
static unsigned long g_camSsidScored  = 0;

// ─── Confidence score ─────────────────────────────────────────────────────────
static int           g_score         = 0;
static unsigned long g_lastDecay     = 0;

// ─── Device persistence tracker ──────────────────────────────────────────────
#define MAX_TRACKED 50
#define PERSIST_MIN_COUNT   3
#define PERSIST_MIN_MS   300000UL  // 5 minutes

struct TrackedDev {
    char     addr[18];
    unsigned long firstSeen;
    unsigned long lastSeen;
    uint16_t seenCount;
    bool     scored;
};
static TrackedDev g_tracked[MAX_TRACKED];
static uint8_t    g_trackedCount = 0;

// ─── Phase / app state ────────────────────────────────────────────────────────
enum Phase    { PHASE_BLE, PHASE_WIFI_SCAN, PHASE_PROMISC };
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
static NimBLEScan* g_pScan = nullptr;

// Returns true if string (lower-cased on the fly) contains keyword
static bool strContainsCI(const char* hay, const char* needle) {
    if (!hay || !needle) return false;
    // build lowercase copy of hay
    char low[64]; size_t i=0;
    while (i<63 && hay[i]) { low[i]=(char)tolower((unsigned char)hay[i]); i++; }
    low[i]='\0';
    return strstr(low, needle) != nullptr;
}

class EyeSpyBLECallbacks : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* adv) override {
        int8_t rssi = adv->getRSSI();
        if (rssi < RSSI_MIN) return;

        unsigned long now = (unsigned long)millis();
        std::string   addr = adv->getAddress().toString(); // "xx:xx:xx:xx:xx:xx"
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

        // ── 3. Flock Safety BLE — device name match ──────────────────────────
        if (!name.empty()) {
            for (const char** kw = FLOCK_BLE_NAMES; *kw; kw++) {
                if (strContainsCI(name.c_str(), *kw)) {
                    g_flockBleDet=true; g_flockBleRssi=rssi; g_flockBleSeen=now;
                    matched=true; break;
                }
            }
        }

        // ── 4. Card skimmer — name exact match HC-03/05/06 ────────────────────
        if (!name.empty()) {
            for (const char** kw = SKIMMER_NAMES; *kw; kw++) {
                if (name == *kw) {
                    g_skimmerDet=true; g_skimmerRssi=rssi; g_skimmerSeen=now;
                    matched=true; break;
                }
            }
        }

        // ── 5. AirTag — Apple mfr data 4C 00 then subtype 0x12 ───────────────
        {
            std::string mfr = adv->getManufacturerData();
            if (mfr.size() >= 3) {
                const uint8_t* m = (const uint8_t*)mfr.data();
                // Apple company = 0x004C (LE: 4C 00)
                if (m[0]==0x4C && m[1]==0x00) {
                    uint8_t subtype = m[2];
                    // 0x12 = FindMy (AirTag / item), 0x1E = newer format
                    if (subtype == 0x12 || subtype == 0x1E) {
                        g_airtagDet=true; g_airtagRssi=rssi; g_airtagSeen=now;
                        matched=true;
                    }
                    // 0x02 = iBeacon (stores, airports, retail tracking)
                    if (subtype == 0x02 && mfr.size() >= 4 && m[3] == 0x15) {
                        g_ibeaconDet=true; g_ibeaconRssi=rssi; g_ibeaconSeen=now;
                        matched=true;
                    }
                }
            }
        }

        // Also check raw payload for AirTag pattern: 1E FF 4C 00 or 4C 00 12 xx
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

        // ── 6. OpenDroneID — svc UUID 0xFFFA ────────────────────────────────
        if (adv->isAdvertisingService(NimBLEUUID((uint16_t)0xFFFA))) {
            g_odidBleDet=true; g_odidBleRssi=rssi; g_odidBleSeen=now; matched=true;
        } else {
            // raw payload fallback: AD type 0x16, UUID FA FF, app code 0x0D
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

        // ── 7. Samsung SmartTag — svc UUID 0xFD5A ────────────────────────────
        if (adv->isAdvertisingService(NimBLEUUID((uint16_t)0xFD5A))) {
            g_smarttagDet=true; g_smarttagRssi=rssi; g_smarttagSeen=now; matched=true;
        }

        // ── 8. Tile tracker — svc UUID 0xFEED or 0xFEEC ──────────────────────
        if (adv->isAdvertisingService(NimBLEUUID((uint16_t)0xFEED)) ||
            adv->isAdvertisingService(NimBLEUUID((uint16_t)0xFEEC))) {
            g_tileDet=true; g_tileRssi=rssi; g_tileSeen=now; matched=true;
        }

        // ── 9. MeshCore — name prefix "MeshCore-" ────────────────────────────
        if (!name.empty() && name.size() >= 9 &&
            name.compare(0, 9, "MeshCore-") == 0) {
            g_meshcoreDet=true; g_meshcoreRssi=rssi; g_meshcoreSeen=now; matched=true;
        }

        // ── 11. Device persistence tracker (unknown devices) ─────────────────
        if (!matched && addr.size() == 17) {
            // Look for existing entry
            bool found = false;
            for (uint8_t i = 0; i < g_trackedCount; i++) {
                if (strcmp(g_tracked[i].addr, addr.c_str()) == 0) {
                    g_tracked[i].lastSeen = now;
                    g_tracked[i].seenCount++;
                    found = true;
                    // Flag if persistent and not yet scored
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

// ─── OUI match helper ─────────────────────────────────────────────────────────
static bool ouiMatch(const uint8_t* bssid, const uint8_t tbl[][3], size_t cnt) {
    for (size_t i = 0; i < cnt; i++)
        if (bssid[0]==tbl[i][0] && bssid[1]==tbl[i][1] && bssid[2]==tbl[i][2])
            return true;
    return false;
}

static bool ssidHas(const char* ssid, const char** kws) {
    if (!ssid || !ssid[0]) return false;
    char low[33]; size_t i=0;
    while (i<32 && ssid[i]) { low[i]=(char)tolower((unsigned char)ssid[i]); i++; }
    low[i]='\0';
    for (const char** kw=kws; *kw; kw++)
        if (strstr(low,*kw)) return true;
    return false;
}

// ─── Scoring ─────────────────────────────────────────────────────────────────
static void addScore(int pts, unsigned long now, unsigned long* ts, const char* tag) {
    if (now - *ts < DETECTION_RESCORE_MS) return;
    *ts = now;
    g_score += pts;
    Serial.printf("[eyespy] +%d (%s)  score=%d\n", pts, tag, g_score);
}

// ─── WiFi scan ───────────────────────────────────────────────────────────────
static void performWifiScan() {
    Serial.println("[eyespy] WiFi scan");
    int n = WiFi.scanNetworks(false, true, false, 120);
    if (n <= 0) { Serial.println("[eyespy] scan 0 nets"); return; }

    unsigned long now = millis();
    bool fAlprOui=false, fFlockSsid=false, fAlprSsid=false,
         fCamOui=false,  fCamSsid=false;

    for (int i = 0; i < n; i++) {
        if (WiFi.RSSI(i) < RSSI_MIN) continue;
        uint8_t*    bssid = WiFi.BSSID(i);
        const char* ssid  = WiFi.SSID(i).c_str();

        if (!fAlprOui && bssid && ouiMatch(bssid, ALPR_OUIS, NUM_ALPR_OUIS)) {
            Serial.printf("[eyespy] ALPR/Flock OUI %02x:%02x:%02x \"%s\"\n",
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

    if (fAlprOui)   addScore(PTS_ALPR_OUI,  now, &g_alprOuiScored,  "ALPR-OUI");
    if (fFlockSsid) addScore(PTS_FLOCK_SSID,now, &g_flockSsidScored,"Flock-SSID");
    if (fAlprSsid)  addScore(PTS_ALPR_SSID, now, &g_alprSsidScored, "ALPR-SSID");
    if (fCamOui)    addScore(PTS_CAM_OUI,   now, &g_camOuiScored,   "cam-OUI");
    if (fCamSsid)   addScore(PTS_CAM_SSID,  now, &g_camSsidScored,  "cam-SSID");
    Serial.printf("[eyespy] WiFi done  score=%d\n", g_score);
}

// ─── Process BLE detections ───────────────────────────────────────────────────
static void processBLE() {
    unsigned long now = millis();

#define CHECK_DET(name, pts, tag) \
    if (g_##name##Det) { \
        g_##name##Det = false; \
        Serial.printf("[eyespy] " tag "  RSSI=%d\n", (int)g_##name##Rssi); \
        addScore(pts, now, &g_##name##Scored, tag); \
    }

    CHECK_DET(axon,     PTS_AXON,     "Axon-cam");
    CHECK_DET(rayban,   PTS_RAYBAN,   "RayBan-Meta");
    CHECK_DET(flockBle, PTS_FLOCK_BLE,"Flock-BLE");
    CHECK_DET(skimmer,  PTS_SKIMMER,  "Skimmer");
    CHECK_DET(airtag,   PTS_AIRTAG,   "AirTag");
    CHECK_DET(odidBle,  PTS_ODID_BLE, "ODID-BLE");
    CHECK_DET(odidWifi, PTS_ODID_WIFI,"ODID-WiFi");
    CHECK_DET(smarttag, PTS_SMARTTAG, "SmartTag");
    CHECK_DET(tile,     PTS_TILE,     "Tile");
    CHECK_DET(meshcore, PTS_MESHCORE, "MeshCore");
    CHECK_DET(ibeacon,  PTS_IBEACON,  "iBeacon");
    CHECK_DET(persist,  PTS_PERSIST,  "PersistTracker");
}

// ─── Score decay ─────────────────────────────────────────────────────────────
static void tickDecay() {
    unsigned long now = millis();
    if (now - g_lastDecay >= SCORE_DECAY_INTERVAL) {
        g_lastDecay = now;
        if (g_score > 0) { g_score--; Serial.printf("[eyespy] decay  score=%d\n", g_score); }
    }
}

// ─── LED update ──────────────────────────────────────────────────────────────
static void updateLED() {
    unsigned long now = millis();
    if (g_state == STATE_STARTUP) {
        bool on = ((now / STARTUP_PULSE_MS) & 1) == 0;
        setLED(0, 0, on ? 40 : 0); return;
    }
    if (g_score >= SCORE_ALERT) {
        bool on = ((now / ALERT_FLASH_HALF_MS) & 1) == 0;
        setLED(on ? 220 : 0, 0, 0);
    } else if (g_score >= SCORE_CAUTION) {
        setLED(180, 60, 0);
    } else {
        setLED(0, 80, 0);
    }
}

// ─── Status print ─────────────────────────────────────────────────────────────
static unsigned long g_lastStatus = 0;
static void printStatus() {
    unsigned long now = millis();
    if (now - g_lastStatus < 10000) return;
    g_lastStatus = now;
    const char* ph = (g_phase==PHASE_BLE) ? "BLE" :
                     (g_phase==PHASE_WIFI_SCAN) ? "WIFI" : "PROMISC";
    const char* st = (g_score>=SCORE_ALERT) ? "ALERT" :
                     (g_score>=SCORE_CAUTION) ? "CAUTION" : "CLEAR";
    Serial.printf("[eyespy] status  score=%d  %s  phase=%s  tracked=%d\n",
                  g_score, st, ph, (int)g_trackedCount);
}

// ─── Purge stale tracked ──────────────────────────────────────────────────────
static void purgeTracked() {
    static unsigned long lastPurge = 0;
    unsigned long now = millis();
    if (now - lastPurge < 60000) return;
    lastPurge = now;
    // remove devices not seen in 30 min
    uint8_t w = 0;
    for (uint8_t i = 0; i < g_trackedCount; i++) {
        if (now - g_tracked[i].lastSeen < 1800000UL)
            g_tracked[w++] = g_tracked[i];
    }
    g_trackedCount = w;
}

// ─── setup() ─────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("[eyespy] Eye Spy v1.1 starting");

    strip.begin();
    strip.setBrightness(80);
    setLED(0, 0, 40);
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    delay(100);

    NimBLEDevice::init("");
    NimBLEDevice::setPower(ESP_PWR_LVL_P3);
    g_pScan = NimBLEDevice::getScan();
    g_pScan->setAdvertisedDeviceCallbacks(&g_bleCallbacks, false);
    g_pScan->setActiveScan(false);
    g_pScan->setInterval(100);
    g_pScan->setWindow(99);

    memset(g_tracked, 0, sizeof(g_tracked));
    g_trackedCount = 0;

    g_startupMs  = millis();
    g_phaseStart = millis();
    g_lastDecay  = millis();
    g_lastStatus = millis();
    g_state      = STATE_STARTUP;

    startBLEScan();
    Serial.println("[eyespy] init OK");
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
            performWifiScan();
            promiscStart();
            g_chanIdx = 0;
            esp_wifi_set_channel(CHANNELS[0], WIFI_SECOND_CHAN_NONE);
            g_phase = PHASE_PROMISC; g_phaseStart = millis();
            break;

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
    delay(10);
}
