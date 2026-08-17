// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 SimeonOnSecurity <https://github.com/simeononsecurity>
//
// es_beacon_test.cpp — "Eye Spy beacon tester" / detection-test firmware.
//
// WHAT THIS IS:
// A standalone firmware (separate PlatformIO environment, NOT the real
// detector) for the M5Stack Atom Lite that cycles through every one of
// eye-spy's 22 detection engines (see main.cpp's file-header table),
// broadcasting a signal byte-for-byte crafted to match each engine's exact
// matching rule in es_detect.h / es_confidence.h — this file is written
// directly against those same tables/checks so it never drifts out of
// sync with what the real detector considers a hit, mirroring the role
// flock-you-esp32's beacon_test.cpp plays for that sibling project.
//
// WHY THIS LOOKS DIFFERENT FROM flock-you-esp32's beacon_test.cpp:
// flock-you-esp32's detector runs a continuous WiFi promiscuous sniffer +
// always-on BLE scan simultaneously (BLE_COEX_MODE), so a ~4-second
// scenario cadence with short (<2s) bursts reliably overlaps it. eye-spy's
// main.cpp instead time-multiplexes a single radio through sequential
// PHASE_BLE (9s) -> PHASE_WIFI_SCAN/WAIT (~3-5s) -> PHASE_PROMISC (5s)
// phases, one full rotation taking roughly 17-19 seconds — and each phase
// only "listens" for its own subset of engines (BLE phase can't see WiFi,
// WiFi-scan phase can't see promiscuous raw frames, etc.). A short burst
// can easily land entirely inside the WRONG phase and be missed. To
// guarantee overlap regardless of where in its cycle the real detector
// currently is, every scenario here is held continuously for
// SCENARIO_HOLD_MS (20s, comfortably longer than one full phase-cycle)
// rather than fired as a brief pulse.
//
// THREE SCENARIO KINDS, THREE TRANSPORTS:
//   1. 12 BLE scenarios       — NimBLE advertising (matches EyeSpyBLECallbacks
//                                ::onResult() in main.cpp).
//   2. 8 WiFi active-scan     — a real SoftAP with a spoofed BSSID/SSID
//      scenarios                (matches processWifiScan() in main.cpp,
//                                which only sees WiFi.scanNetworks() results
//                                — NOT raw frames — so unlike
//                                flock-you-esp32's WiFi scenarios, these
//                                cannot be done via esp_wifi_80211_tx()).
//   3. 1 WiFi promiscuous     — raw 802.11 frame injection with the
//      scenario (odidWifi)      OpenDroneID NaN destination MAC (matches
//                                wifiSniffer() in main.cpp — the only WiFi
//                                engine that inspects raw frames instead of
//                                scan results).
//
// THE 22nd ENGINE (persist) IS SPECIAL-CASED, NOT IN THE ROTATION:
// `persist` requires the SAME unclassified BLE MAC to be seen >= 3 times
// spanning >= 5 minutes (PERSIST_MIN_COUNT/PERSIST_MIN_MS in
// es_confidence.h) — a behavioral/longitudinal check, not a single-shot
// "matches this payload" check. Folding it into the same shuffled 20s-hold
// rotation as everything else would mean it only gets a chance to fire
// once every full rotation (~21 scenarios x 20s = ~7 minutes), so 3 hits
// spanning 5 minutes would take 3 full rotations (~21 minutes) in the
// worst case. Instead it runs on its own independent, more frequent timer
// (PERSIST_INTERVAL_MS, 2.5 min) using a fixed BLE address generated once
// at boot (so the real detector's persistence tracker sees the *same* MAC
// each time) and a payload that deliberately matches none of the other 21
// engines (so `matched` stays false in onResult() and the persist branch
// actually runs). It's still exercised via the exact same code path
// (bleAdvertiseAndHold()) as the rotation scenarios — just scheduled
// separately so 3 sightings across >=5 minutes reliably happens within a
// single ~10-minute test session instead of requiring ~20+ minutes.
//
// HOW TO USE:
//   1. Flash this to a SEPARATE M5Stack Atom Lite:
//        pio run -e atom-lite-beacon -t upload
//   2. Power it on near your real eye-spy detector (running the `atom-lite`
//      — or any other — environment's normal firmware).
//   3. It cycles through 21 rotation scenarios automatically (shuffled
//      order, full coverage every pass) every SCENARIO_HOLD_MS, plus fires
//      the persist scenario on its own separate timer. Press the button
//      (GPIO39) to skip the current scenario's remaining hold time and
//      move to the next one immediately.
//   4. Watch the real detector's serial log / LED / score for each hit.
//      Serial output here logs exactly which scenario just fired and, for
//      WiFi/BLE identity fields, what value was used.
//
// DO NOT flash this to the board you intend to use as a real detector —
// like flock-you-esp32's beacon_test.cpp, this firmware only transmits
// fake signals; it does not scan or score anything itself.

#include <Arduino.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include <NimBLEDevice.h>
#include <string>
#include <cstring>

// Same conditional include path NimBLE-Arduino's own headers use (see
// NimBLEScan.h) — the plain "host/ble_hs_id.h" form only resolves when
// building against ESP-IDF's native NimBLE component (CONFIG_NIMBLE_CPP_IDF),
// which arduino-esp32 framework builds don't define. Needed for
// ble_hs_id_set_rnd()/ble_hs_id_gen_rnd() to construct the axon scenario's
// exact chosen BLE address OUI and the persist scenario's stable address.
#if defined(CONFIG_NIMBLE_CPP_IDF)
#include "host/ble_hs_id.h"
#else
#include "nimble/nimble/host/include/host/ble_hs_id.h"
#endif

// Pattern tables shared with the real detector — keeps this tool
// automatically in sync with es_detect.h if the OUI/SSID/UUID/name tables
// are ever updated. Pure/dependency-free header, safe to include directly.
#include "es_detect.h"
#include "es_beacon_frames.h"

// ---- Board config: M5Stack Atom Lite (NeoPixel LED GPIO27, button GPIO39) ----
// USE_LED must be defined before led_neopixel.h is included (that header's
// stub/no-op fallback path only activates when USE_LED is *not* truthy).
#define USE_LED 1
#include "led_neopixel.h"

#define BUTTON_PIN 39

// ============================================================
// CONFIG
// ============================================================

// Hold duration for every rotation scenario (BLE adv / SoftAP / raw-frame
// sweep). Must exceed eye-spy's full phase-cycle length (~17-19s: 9s BLE +
// ~3-5s WiFi scan+wait + 5s promiscuous) so a scenario starting at ANY
// point in the real detector's cycle is still active once its matching
// phase comes back around — see file-header comment for why short pulses
// (which work fine for flock-you-esp32's continuously-listening detector)
// don't work here.
#define SCENARIO_HOLD_MS      20000UL

// persist needs >=3 sightings spanning >=5 min (PERSIST_MIN_COUNT/
// PERSIST_MIN_MS in es_confidence.h). Firing every 2.5 min means the 3rd
// fire lands ~5 min after the 1st, satisfying the requirement inside a
// single ~10-minute test session instead of needing ~3 full 21-scenario
// rotations (~20+ minutes) if it were folded into the shuffled rotation.
#define PERSIST_INTERVAL_MS  150000UL

// Promiscuous raw-frame channel sweep — mirrors eye-spy's own
// PHASE_PROMISC channel list (main.cpp's CHANNELS[]) so the burst is sent
// on every channel the real detector's channelHop() will dwell on.
static const uint8_t ODID_CHANNELS[] = {1, 6, 11, 3, 8, 13};
#define ODID_CHANNELS_COUNT (sizeof(ODID_CHANNELS)/sizeof(ODID_CHANNELS[0]))
#define ODID_BURST_GAP_MS    20UL

static const uint8_t NAN_DEST_MAC[6]  = {0x51,0x6f,0x9a,0x01,0x00,0x00};
static const uint8_t ODID_SRC_MAC[6]  = {0x02,0x45,0x53,0x00,0x0d,0x00}; // arbitrary LAA "ES" test MAC

// Generic locally-administered MAC prefix used as the SoftAP BSSID for the
// two SSID-keyword-only scenarios (flockSsid/alprSsid/camSsid), where the
// BSSID value itself is irrelevant to the check being exercised — just
// needs to be a valid, non-multicast unicast address.
static const uint8_t GENERIC_AP_PREFIX[3] = {0x02, 0x45, 0x53}; // "ES" LAA test prefix

// ============================================================
// STATE
// ============================================================

static NimBLEAdvertising* g_pAdv = nullptr;

static bool          btnLastState    = true;  // idle = HIGH (pulled up)
static unsigned long btnLastChangeMs = 0;
#define BTN_DEBOUNCE_MS 50

static unsigned long ledOffAt = 0;

static uint8_t  g_persistAddr[6];
static bool     g_persistAddrValid = false;
static uint32_t g_persistFireCount = 0;
static unsigned long g_lastPersistFireAt = 0;

// ============================================================
// SMALL HELPERS
// ============================================================

static bool buttonPressed() {
  bool cur = digitalRead(BUTTON_PIN);
  unsigned long now = millis();
  if (cur != btnLastState && (now - btnLastChangeMs) > BTN_DEBOUNCE_MS) {
    btnLastChangeMs = now;
    btnLastState     = cur;
    if (!cur) return true;  // LOW == pressed
  }
  return false;
}

static void ledPulse(uint8_t r, uint8_t g, uint8_t b, unsigned ms) {
  setLED(r, g, b);
  ledOffAt = millis() + ms;
}

static void ledTick() {
  if (ledOffAt && (long)(millis() - ledOffAt) >= 0) {
    setLED(0, 0, 0);
    ledOffAt = 0;
  }
}

// Blocks for holdMs, keeping the LED pulse timing serviced and allowing the
// button to cut the hold short (skip to the next scenario immediately).
// Returns true if the button interrupted the hold early.
static bool holdWithButtonPoll(uint32_t holdMs) {
  unsigned long start = millis();
  while (millis() - start < holdMs) {
    ledTick();
    if (buttonPressed()) return true;
    delay(20);
  }
  return false;
}

static void macToStr(const uint8_t* mac, char* buf, size_t len) {
  snprintf(buf, len, "%02x:%02x:%02x:%02x:%02x:%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// Builds a 6-byte MAC from a 3-byte OUI table entry + 3 random suffix
// bytes. esp_wifi_set_mac() rejects addresses with the multicast bit (bit0
// of the first byte) set — all real vendor-assigned OUIs already have
// bit0=0 (multicast MACs are never OUI-assigned to a vendor for unicast
// devices), but bit0 is cleared defensively anyway so a future table edit
// can never silently break SoftAP MAC-spoofing here.
static void macFromOui(const uint8_t oui[3], uint8_t* mac6) {
  mac6[0] = oui[0] & 0xFE;
  mac6[1] = oui[1];
  mac6[2] = oui[2];
  mac6[3] = (uint8_t)random(0, 256);
  mac6[4] = (uint8_t)random(0, 256);
  mac6[5] = (uint8_t)random(0, 256);
}

static void pickRandomOuiEntry(const uint8_t tbl[][3], size_t cnt, uint8_t* mac6) {
  const uint8_t* e = tbl[random(0, (long)cnt)];
  macFromOui(e, mac6);
}

// ============================================================
// BLE ADVERTISING
// ============================================================

// Fresh NimBLEAdvertisementData object per call (avoids stale-field
// accumulation across scenarios, since each set*() call appends directly
// to the object's internal payload buffer).
static void bleAdvertiseAndHold(NimBLEAdvertisementData& data, uint32_t holdMs) {
  g_pAdv->stop();
  g_pAdv->setAdvertisementData(data);
  bool started = g_pAdv->start();
  if (!started) {
    // Clean Code rule: never silently swallow a bool return that signals a
    // real transmission failure — this project has been bitten by exactly
    // this class of bug before (adv->start() ignored in an earlier
    // flock-you-esp32 revision masked a completely silent BLE TX failure).
    Serial.println("[esbeacon] adv->start() FAILED -- nothing was actually "
                    "transmitted this scenario!");
  }
  holdWithButtonPoll(holdMs);
  g_pAdv->stop();
}

// ============================================================
// WIFI: SoftAP MAC/SSID spoofing (active-scan-detectable engines)
// ============================================================

// Spoofs the SoftAP's BSSID before starting it so eye-spy's
// WiFi.scanNetworks() sees an AP with an OUI-matching (or otherwise
// pattern-matching) source MAC + SSID.
//
// ORDERING NOTE: esp_wifi_set_mac()'s doc comment (esp_wifi.h) states the
// target interface must be "disabled" when called. WiFi.mode(WIFI_AP)
// brings the AP netif up (starts the WiFi driver in AP mode) but does NOT
// yet start beaconing/broadcasting — that only happens once WiFi.softAP()
// is called (which internally applies the AP config and actually starts
// the interface) — so calling esp_wifi_set_mac() in between satisfies the
// "must be disabled" constraint. This is the standard, widely-documented
// ESP32/Arduino sequence for spoofing a SoftAP's MAC address.
static void wifiApHoldWithMac(const uint8_t mac[6], const char* ssid, uint32_t holdMs) {
  WiFi.mode(WIFI_AP);
  esp_err_t rc = esp_wifi_set_mac(WIFI_IF_AP, mac);
  if (rc != ESP_OK) {
    Serial.printf("[esbeacon] esp_wifi_set_mac FAILED rc=%d -- AP will broadcast "
                  "with its real MAC instead of the spoofed OUI this scenario\n", (int)rc);
  }
  WiFi.softAP(ssid);
  Serial.printf("[esbeacon] SoftAP up  ssid=\"%s\"  bssid=%02x:%02x:%02x:%02x:%02x:%02x\n",
                ssid, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  holdWithButtonPoll(holdMs);
  WiFi.softAPdisconnect(true);
  // Restore the STA/disconnected baseline so subsequent BLE and
  // promiscuous-raw-tx scenarios see a clean, predictable WiFi mode.
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(50);
}

// ============================================================
// BLE SCENARIOS (12 in the shuffled rotation; persist is separate — see
// scenarioPersist() below)
// ============================================================

// axon — main.cpp matches on the advertiser's own BLE *address* OUI
// (00:25:df), not anything in the payload. Constructing this exact address
// requires calling ble_hs_id_set_rnd() directly with a manually-built
// 6-byte array, rather than ble_hs_id_gen_rnd() (which only ever produces
// a RANDOM address, with no way to pin a chosen OUI prefix).
//
// BYTE ORDER: ble_addr_t.val[6] is stored in REVERSE order relative to the
// human-readable "xx:xx:xx:xx:xx:xx" string form -- confirmed via
// NimBLEAddress::operator std::string(), which prints
// val[5]:val[4]:val[3]:val[2]:val[1]:val[0]. So to produce the printed
// address "00:25:df:xx:xx:xx", val[5]=0x00, val[4]=0x25, val[3]=0xdf.
//
// ble_hs_id_set_rnd() (nimble/host/src/ble_hs_id.c) requires the top 2
// bits of val[5] to be either 0x00 (NRPA) or 0xc0 (static random) -- 0x00
// satisfies this automatically since that's exactly the literal value we
// want for the first printed octet anyway, so no extra masking is needed.
static void scenarioAxon() {
  uint8_t addr[6];
  addr[5] = 0x00; addr[4] = 0x25; addr[3] = 0xdf;
  addr[2] = (uint8_t)random(0, 256);
  addr[1] = (uint8_t)random(0, 256);
  addr[0] = (uint8_t)random(0, 256);

  int rc = ble_hs_id_set_rnd(addr);
  if (rc != 0) {
    Serial.printf("[esbeacon] axon: ble_hs_id_set_rnd FAILED rc=%d -- "
                  "scenario skipped (address not applied)\n", rc);
    return;
  }
  NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);

  NimBLEAdvertisementData data;
  data.setName("ESTest");
  Serial.printf("[esbeacon] axon  addr=%s (fake Axon body-camera OUI 00:25:df)\n",
                NimBLEDevice::getAddress().toString().c_str());
  bleAdvertiseAndHold(data, SCENARIO_HOLD_MS);

  NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_PUBLIC);
}

static void scenarioRayban() {
  NimBLEAdvertisementData data;
  data.setCompleteServices(NimBLEUUID((uint16_t)0xFD5F));
  Serial.println("[esbeacon] rayban  svc=0xFD5F");
  bleAdvertiseAndHold(data, SCENARIO_HOLD_MS);
}

static void scenarioFlockBle() {
  NimBLEAdvertisementData data;
  data.setName("Flock-ESTest");
  Serial.println("[esbeacon] flockBle  name=\"Flock-ESTest\"");
  bleAdvertiseAndHold(data, SCENARIO_HOLD_MS);
}

static void scenarioFlockBleMfr() {
  uint8_t mfr[3] = {0xC8, 0x09, 0x00};  // LE company ID 0x09C8 (XUNTONG/Flock)
  NimBLEAdvertisementData data;
  data.setManufacturerData(std::string((char*)mfr, sizeof(mfr)));
  Serial.println("[esbeacon] flockBleMfr  id=0x09C8");
  bleAdvertiseAndHold(data, SCENARIO_HOLD_MS);
}

static void scenarioRavenBle() {
  int n = 0; while (RAVEN_UUIDS[n]) n++;
  const char* uuid = RAVEN_UUIDS[random(0, n)];
  NimBLEAdvertisementData data;
  data.setCompleteServices(NimBLEUUID(uuid));
  Serial.printf("[esbeacon] ravenBle  uuid=%s\n", uuid);
  bleAdvertiseAndHold(data, SCENARIO_HOLD_MS);
}

static void scenarioSkimmer() {
  int n = 0; while (SKIMMER_NAMES[n]) n++;
  const char* name = SKIMMER_NAMES[random(0, n)];
  NimBLEAdvertisementData data;
  data.setName(name);
  Serial.printf("[esbeacon] skimmer  name=%s\n", name);
  bleAdvertiseAndHold(data, SCENARIO_HOLD_MS);
}

static void scenarioAirtag() {
  uint8_t mfr[3] = {0x4C, 0x00, 0x12};  // Apple company ID + FindMy/AirTag subtype
  NimBLEAdvertisementData data;
  data.setManufacturerData(std::string((char*)mfr, sizeof(mfr)));
  Serial.println("[esbeacon] airtag  mfr=4C:00:12");
  bleAdvertiseAndHold(data, SCENARIO_HOLD_MS);
}

static void scenarioOdidBle() {
  NimBLEAdvertisementData data;
  data.setCompleteServices(NimBLEUUID((uint16_t)0xFFFA));
  Serial.println("[esbeacon] odidBle  svc=0xFFFA");
  bleAdvertiseAndHold(data, SCENARIO_HOLD_MS);
}

static void scenarioSmarttag() {
  NimBLEAdvertisementData data;
  data.setCompleteServices(NimBLEUUID((uint16_t)0xFD5A));
  Serial.println("[esbeacon] smarttag  svc=0xFD5A");
  bleAdvertiseAndHold(data, SCENARIO_HOLD_MS);
}

static void scenarioTile() {
  uint16_t uuid = random(0, 2) ? 0xFEED : 0xFEEC;
  NimBLEAdvertisementData data;
  data.setCompleteServices(NimBLEUUID(uuid));
  Serial.printf("[esbeacon] tile  svc=0x%04X\n", uuid);
  bleAdvertiseAndHold(data, SCENARIO_HOLD_MS);
}

static void scenarioMeshcore() {
  NimBLEAdvertisementData data;
  data.setName("MeshCore-ESTest");
  Serial.println("[esbeacon] meshcore  name=\"MeshCore-ESTest\"");
  bleAdvertiseAndHold(data, SCENARIO_HOLD_MS);
}

static void scenarioIbeacon() {
  uint8_t mfr[4] = {0x4C, 0x00, 0x02, 0x15};  // Apple company ID + iBeacon type/len
  NimBLEAdvertisementData data;
  data.setManufacturerData(std::string((char*)mfr, sizeof(mfr)));
  Serial.println("[esbeacon] ibeacon  mfr=4C:00:02:15");
  bleAdvertiseAndHold(data, SCENARIO_HOLD_MS);
}

// persist — see file-header comment for why this runs on its own timer
// instead of the shuffled rotation. Uses a FIXED address (generated once
// at boot via ble_hs_id_gen_rnd(), the same "static random" address every
// firing) and a manufacturer ID (0xFFFF, IEEE-reserved/unassigned) that
// deliberately matches none of the other 21 engines, so main.cpp's
// `matched` stays false and the persistence-tracker branch actually runs.
static void scenarioPersist() {
  if (!g_persistAddrValid) {
    Serial.println("[esbeacon] persist  SKIPPED -- ble_hs_id_gen_rnd failed at boot");
    return;
  }
  int rc = ble_hs_id_set_rnd(g_persistAddr);
  if (rc != 0) {
    Serial.printf("[esbeacon] persist  ble_hs_id_set_rnd FAILED rc=%d -- "
                  "skipped this firing\n", rc);
    return;
  }
  NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);

  uint8_t mfr[3] = {0xFF, 0xFF, 0x00};  // unassigned company ID -- matches nothing
  NimBLEAdvertisementData data;
  data.setManufacturerData(std::string((char*)mfr, sizeof(mfr)));

  Serial.printf("[esbeacon] persist  addr=%s  fire #%u (needs >=3 fires spanning "
                ">=5 min -- see file header)\n",
                NimBLEDevice::getAddress().toString().c_str(),
                (unsigned)(++g_persistFireCount));
  bleAdvertiseAndHold(data, SCENARIO_HOLD_MS);

  NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_PUBLIC);
}

// ============================================================
// WIFI ACTIVE-SCAN SCENARIOS (8) — spoofed SoftAP BSSID/SSID
// ============================================================

static void scenarioFlockOui() {
  uint8_t mac[6]; pickRandomOuiEntry(FLOCK_OUIS, NUM_FLOCK_OUIS, mac);
  char s[18]; macToStr(mac, s, sizeof(s));
  Serial.printf("[esbeacon] flockOui  bssid=%s\n", s);
  wifiApHoldWithMac(mac, "ES-Test-FlockOUI", SCENARIO_HOLD_MS);
}

static void scenarioAlprOui() {
  uint8_t mac[6]; pickRandomOuiEntry(ALPR_OUIS, NUM_ALPR_OUIS, mac);
  char s[18]; macToStr(mac, s, sizeof(s));
  Serial.printf("[esbeacon] alprOui  bssid=%s\n", s);
  wifiApHoldWithMac(mac, "ES-Test-ALPR-OUI", SCENARIO_HOLD_MS);
}

static void scenarioFlockSsid() {
  uint8_t mac[6]; macFromOui(GENERIC_AP_PREFIX, mac);
  Serial.println("[esbeacon] flockSsid  ssid=\"ES-Flock-Test\"");
  wifiApHoldWithMac(mac, "ES-Flock-Test", SCENARIO_HOLD_MS);
}

static void scenarioAlprSsid() {
  uint8_t mac[6]; macFromOui(GENERIC_AP_PREFIX, mac);
  Serial.println("[esbeacon] alprSsid  ssid=\"ES-ALPR-Test\"");
  wifiApHoldWithMac(mac, "ES-ALPR-Test", SCENARIO_HOLD_MS);
}

static void scenarioSoundthinkingOui() {
  uint8_t mac[6]; pickRandomOuiEntry(SOUNDTHINKING_OUIS, NUM_SOUNDTHINKING_OUIS, mac);
  char s[18]; macToStr(mac, s, sizeof(s));
  Serial.printf("[esbeacon] soundthinkingOui  bssid=%s\n", s);
  wifiApHoldWithMac(mac, "ES-Test-ST-OUI", SCENARIO_HOLD_MS);
}

static void scenarioCamOui() {
  uint8_t mac[6]; pickRandomOuiEntry(CAM_OUIS, NUM_CAM_OUIS, mac);
  char s[18]; macToStr(mac, s, sizeof(s));
  Serial.printf("[esbeacon] camOui  bssid=%s\n", s);
  wifiApHoldWithMac(mac, "ES-Test-CamOUI", SCENARIO_HOLD_MS);
}

static void scenarioCamSsid() {
  uint8_t mac[6]; macFromOui(GENERIC_AP_PREFIX, mac);
  Serial.println("[esbeacon] camSsid  ssid=\"ES-CCTV-Test\"");
  wifiApHoldWithMac(mac, "ES-CCTV-Test", SCENARIO_HOLD_MS);
}

static void scenarioFlockMfrOui() {
  uint8_t mac[6]; pickRandomOuiEntry(FLOCK_MFR_OUIS, NUM_FLOCK_MFR_OUIS, mac);
  char s[18]; macToStr(mac, s, sizeof(s));
  Serial.printf("[esbeacon] flockMfrOui  bssid=%s\n", s);
  wifiApHoldWithMac(mac, "ES-Test-MfrOUI", SCENARIO_HOLD_MS);
}

// ============================================================
// WIFI PROMISCUOUS SCENARIO (1) — odidWifi, raw frame injection
// ============================================================

static void scenarioOdidWifi() {
  uint8_t buf[ES_BF_MAX_FRAME];
  size_t len = esbfBuildBeacon(buf, NAN_DEST_MAC, ODID_SRC_MAC, ODID_SRC_MAC, "ES-ODID-Test");
  Serial.println("[esbeacon] odidWifi  addr1=51:6f:9a:01:00:00 (OpenDroneID NaN dest)");

  unsigned long start = millis();
  uint8_t ci = 0;
  while (millis() - start < SCENARIO_HOLD_MS) {
    ledTick();
    esp_wifi_set_channel(ODID_CHANNELS[ci], WIFI_SECOND_CHAN_NONE);
    esp_wifi_80211_tx(WIFI_IF_STA, buf, (int)len, false);
    ci = (uint8_t)((ci + 1) % ODID_CHANNELS_COUNT);
    if (buttonPressed()) break;
    delay(ODID_BURST_GAP_MS);
  }
}

// ============================================================
// SCENARIO ROTATION TABLE
// ============================================================

enum ScenarioGroup { GROUP_BLE, GROUP_WIFI_AP, GROUP_WIFI_PROMISC };

struct ScenarioEntry {
  void (*fn)();
  ScenarioGroup group;
};

#define NUM_SCENARIOS 21
static const ScenarioEntry SCENARIOS[NUM_SCENARIOS] = {
  { scenarioAxon,             GROUP_BLE },
  { scenarioRayban,           GROUP_BLE },
  { scenarioFlockBle,         GROUP_BLE },
  { scenarioFlockBleMfr,      GROUP_BLE },
  { scenarioRavenBle,         GROUP_BLE },
  { scenarioSkimmer,          GROUP_BLE },
  { scenarioAirtag,           GROUP_BLE },
  { scenarioOdidBle,          GROUP_BLE },
  { scenarioSmarttag,         GROUP_BLE },
  { scenarioTile,             GROUP_BLE },
  { scenarioMeshcore,         GROUP_BLE },
  { scenarioIbeacon,          GROUP_BLE },
  { scenarioFlockOui,         GROUP_WIFI_AP },
  { scenarioAlprOui,          GROUP_WIFI_AP },
  { scenarioFlockSsid,        GROUP_WIFI_AP },
  { scenarioAlprSsid,         GROUP_WIFI_AP },
  { scenarioSoundthinkingOui, GROUP_WIFI_AP },
  { scenarioCamOui,           GROUP_WIFI_AP },
  { scenarioCamSsid,          GROUP_WIFI_AP },
  { scenarioFlockMfrOui,      GROUP_WIFI_AP },
  { scenarioOdidWifi,         GROUP_WIFI_PROMISC },
};

static uint8_t scenarioOrder[NUM_SCENARIOS];
static uint8_t scenarioPos = NUM_SCENARIOS;  // force reshuffle on first loop

static void reshuffleScenarios() {
  for (uint8_t i = 0; i < NUM_SCENARIOS; i++) scenarioOrder[i] = i;
  for (int i = NUM_SCENARIOS - 1; i > 0; i--) {
    int j = random(0, i + 1);
    uint8_t t = scenarioOrder[i];
    scenarioOrder[i] = scenarioOrder[j];
    scenarioOrder[j] = t;
  }
  scenarioPos = 0;
}

static void fireNextScenario() {
  if (scenarioPos >= NUM_SCENARIOS) reshuffleScenarios();
  uint8_t idx = scenarioOrder[scenarioPos++];
  const ScenarioEntry& s = SCENARIOS[idx];
  switch (s.group) {
    case GROUP_BLE:          ledPulse(0, 0, 60, 200);  break;  // blue
    case GROUP_WIFI_AP:      ledPulse(60, 0, 60, 200); break;  // purple
    case GROUP_WIFI_PROMISC: ledPulse(60, 30, 0, 200); break;  // orange
  }
  s.fn();
}

// Generates the persist scenario's fixed test address once at boot (a
// "static random" BLE address distinct from the chip's real public
// address), so the real detector's persistence tracker sees a stable MAC
// across every persist-scenario firing, exactly like a real unclassified
// device lingering nearby would.
static void initPersistAddr() {
  ble_addr_t addr;
  int rc = ble_hs_id_gen_rnd(0 /* static random, not NRPA */, &addr);
  if (rc == 0) {
    memcpy(g_persistAddr, addr.val, 6);
    g_persistAddrValid = true;
  } else {
    Serial.printf("[esbeacon] ble_hs_id_gen_rnd FAILED rc=%d -- persist scenario "
                  "will be skipped\n", rc);
  }
}

// ============================================================
// SETUP / LOOP
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("[esbeacon] *** EYE SPY BEACON TESTER *** -- this device only "
                  "transmits fake detection signals. DO NOT use this build as a "
                  "real detector.");

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  btnLastState = digitalRead(BUTTON_PIN);

  ledBegin(80);
  setLED(0, 0, 60); delay(300); setLED(0, 0, 0);

  randomSeed((uint32_t)esp_random());

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(100);

  NimBLEDevice::init("ESTest");
  NimBLEDevice::setPower(ESP_PWR_LVL_P3);
  g_pAdv = NimBLEDevice::getAdvertising();

  initPersistAddr();

  reshuffleScenarios();
  g_lastPersistFireAt = millis();

  Serial.printf("[esbeacon] ready -- %d rotation scenarios (blue=BLE, "
                "purple=WiFi-AP, orange=WiFi-promisc), each held ~%lus "
                "(+ persist fires separately every %lus). "
                "button (GPIO%d) = skip to next scenario now.\n",
                NUM_SCENARIOS, (unsigned long)(SCENARIO_HOLD_MS/1000),
                (unsigned long)(PERSIST_INTERVAL_MS/1000), BUTTON_PIN);
}

void loop() {
  ledTick();

  if (millis() - g_lastPersistFireAt >= PERSIST_INTERVAL_MS) {
    ledPulse(60, 60, 0, 200);  // yellow
    scenarioPersist();
    g_lastPersistFireAt = millis();
  }

  fireNextScenario();

  delay(5);
}
