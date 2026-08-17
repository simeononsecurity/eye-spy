// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 SimeonOnSecurity <https://github.com/simeononsecurity>
//
// es_beacon_frames.h — minimal raw 802.11 management-frame builder, used
// ONLY by es_beacon_test.cpp's single WiFi-promiscuous scenario (odidWifi).
//
// WHY THIS FILE EXISTS / WHY IT'S SO MUCH SMALLER THAN flock-you-esp32's
// beacon_frames.h: eye-spy's wifiSniffer() (main.cpp) only inspects the
// destination address (addr1) of promiscuous-mode management frames,
// looking for the fixed OpenDroneID NaN broadcast MAC
// (51:6f:9a:01:00:00) — it doesn't care about frame subtype, SSID
// content, or any other field. flock-you-esp32's beacon_frames.h supports
// several frame subtypes/IEs because that project's wifiSniffer() parses
// 9 distinct WiFi raw-frame scenarios (OUI in addr1/addr2/addr3, wildcard
// probes, SSID keyword matches, etc.). Here, only ONE WiFi engine
// (odidWifi) needs raw frame injection at all — the other 8 WiFi-
// detectable engines in eye-spy are active-scan (BSSID/SSID) based and are
// tested via a real SoftAP instead (see wifiApHoldWithMac() in
// es_beacon_test.cpp), not raw frame injection. Hence one minimal
// single-purpose Beacon-frame builder is all that's needed.
//
// No FCS is written to the buffer — esp_wifi_80211_tx() has the radio
// hardware append the FCS automatically (per ESP-IDF docs), same as
// flock-you-esp32's beacon_frames.h relies on.

#pragma once

#include <stdint.h>
#include <string.h>

#define ES_BF_MAX_FRAME 128
#define ES_BF_FC_BEACON 0x0080  // type=0 (mgmt), subtype=8 (Beacon)

typedef struct __attribute__((packed)) {
  uint16_t frame_ctrl;
  uint16_t duration;
  uint8_t  addr1[6];   // receiver / destination
  uint8_t  addr2[6];   // transmitter / source
  uint8_t  addr3[6];   // BSSID
  uint16_t seq_ctrl;
} esbf_80211_hdr_t;

static uint16_t esbf_seq_counter = 0;

// Builds a minimal Beacon frame: 24-byte header + 12-byte fixed params
// (timestamp/interval/capability) + SSID IE + Rates IE. addr1 is the only
// field eye-spy's odidWifi check actually inspects, but a syntactically
// valid frame is built anyway so it isn't dropped by any radio-level
// sanity check on transmit. Returns the total frame length written.
static size_t esbfBuildBeacon(uint8_t* buf, const uint8_t* addr1,
                               const uint8_t* addr2, const uint8_t* addr3,
                               const char* ssid) {
  esbf_80211_hdr_t* hdr = (esbf_80211_hdr_t*)buf;
  hdr->frame_ctrl = ES_BF_FC_BEACON;
  hdr->duration   = 0;
  memcpy(hdr->addr1, addr1, 6);
  memcpy(hdr->addr2, addr2, 6);
  memcpy(hdr->addr3, addr3, 6);
  hdr->seq_ctrl = (uint16_t)(esbf_seq_counter++ << 4);

  size_t off = sizeof(esbf_80211_hdr_t);
  memset(buf + off, 0, 8); off += 8;      // timestamp (value irrelevant)
  buf[off++] = 0x64; buf[off++] = 0x00;   // beacon interval = 100 TU
  buf[off++] = 0x01; buf[off++] = 0x00;   // capability info (ESS bit set)

  uint8_t slen = ssid ? (uint8_t)strnlen(ssid, 32) : 0;
  buf[off++] = 0x00; buf[off++] = slen;   // SSID IE (id=0)
  if (slen) { memcpy(buf + off, ssid, slen); off += slen; }

  static const uint8_t rates[] = { 0x82, 0x84, 0x8b, 0x96 };
  buf[off++] = 0x01; buf[off++] = (uint8_t)sizeof(rates);  // Rates IE (id=1)
  memcpy(buf + off, rates, sizeof(rates)); off += sizeof(rates);

  return off;
}
