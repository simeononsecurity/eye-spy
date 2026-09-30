// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2024 SimeonOnSecurity <https://github.com/simeononsecurity>
//
// es_log.h — on-device detection log (see docs/adr/0003-on-device-detection-log.md)
//
// WHY THIS FILE EXISTS: this firmware persists nothing, so a crash or power cut
// erases the entire history — and when a unit is run standalone that history is
// the ONLY record that exists. A field unit rebooting on its own could not be
// diagnosed for exactly this reason: the reset reason and coredump work says
// *that* it died, this says what it was doing beforehand.
//
// The design decisions (all recorded in ADR-0003) are:
//   * storage is a raw partition of FIXED-SIZE, self-validating slots in a ring,
//     so a torn write during a flush invalidates exactly one record and the
//     reader never has to resynchronise over partially written text;
//   * the stored bytes are the SAME text lines this firmware already prints for
//     scored detections, because `api/eyespy.py` already parses them and ADR-0002
//     already versions the format — so a dump replays through the existing parser
//     with no new format, decoder or API change;
//   * appends go to a RAM buffer and are flushed from a periodic tick, never from
//     the detection path, so flash timing stays out of CHECK_DET()/addScore().
//
// THIS HEADER MUST STAY HOST-TESTABLE. Everything above the ARDUINO guard is
// pure logic (no Arduino, no esp_partition) so `pio test -e native` covers the
// slot framing, validation and ring arithmetic; the flash I/O below the guard is
// thin and has no decisions in it worth testing.

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

// ── Slot layout (128 bytes, fixed) ───────────────────────────────────────────
//   [0]       0xA5 magic — first byte checked when validating a slot
//   [1]       reserved (0)
//   [2..3]    u16 text length, 1..ESL_TEXT_MAX
//   [4..5]    u16 CRC-16/CCITT-FALSE over the text bytes
//   [6..9]    u32 uptime seconds when the record was created
//   [10..127] text, NOT NUL-terminated (the length field is authoritative)
//
// A magic byte per slot is what makes a torn write cheap to detect: a slot whose
// magic, length or CRC is wrong is simply skipped. Without it, a half-written
// record would be indistinguishable from text.
#define ESL_MAGIC_SLOT   0xA5
#define ESL_SLOT_SIZE    128
#define ESL_SLOT_HDR     10
#define ESL_TEXT_MAX     (ESL_SLOT_SIZE - ESL_SLOT_HDR)

// Partition header (16 bytes): magic, version, reserved, head, total.
//   head  = next slot index to write (0..capacity-1)
//   total = records ever written, saturating at UINT32_MAX
// Valid record count is min(total, capacity); "lost" is total - capacity once the
// ring has wrapped. Keeping only these two counters means there is no third value
// that can disagree with them.
#define ESL_MAGIC_PART   0x474C5345u   // 'E','S','L','G'
#define ESL_HDR_SIZE     16
#define ESL_VERSION      1

static inline uint16_t eslogCrc16(const uint8_t* d, size_t n) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < n; i++) {
    crc ^= (uint16_t)d[i] << 8;
    for (int b = 0; b < 8; b++)
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
  }
  return crc;
}

// How many slots fit in a partition of partSize bytes. 0 means "too small to
// use" — callers treat that as "logging disabled" rather than dividing by zero.
static inline uint32_t eslogCapacity(uint32_t partSize) {
  if (partSize <= ESL_HDR_SIZE) return 0;
  return (partSize - ESL_HDR_SIZE) / ESL_SLOT_SIZE;
}

static inline uint32_t eslogSlotOff(uint32_t slot) {
  return ESL_HDR_SIZE + slot * ESL_SLOT_SIZE;
}

// Records currently readable: min(total, capacity).
static inline uint32_t eslogCount(uint32_t total, uint32_t capacity) {
  return (total < capacity) ? total : capacity;
}

// Slot index of the OLDEST readable record. Before the ring wraps that is slot 0;
// afterwards it is `head`, since head is the next slot to be overwritten.
static inline uint32_t eslogOldest(uint32_t total, uint32_t capacity, uint32_t head) {
  return (total < capacity) ? 0u : (head % capacity);
}

// Next slot index, wrapping. Kept here so the ring arithmetic has one home.
static inline uint32_t eslogNextSlot(uint32_t head, uint32_t capacity) {
  if (capacity == 0) return 0;
  uint32_t n = head + 1;
  return (n >= capacity) ? 0 : n;
}

// Serialise one record into a slot buffer. Returns the text length written, or 0
// if the text is empty or too long — the caller then logs nothing rather than
// silently truncating, because a truncated line may no longer parse back into a
// detection and would quietly corrupt a replay.
static inline uint16_t eslogBuildSlot(uint8_t* out, const char* text, uint32_t uptime) {
  if (!out || !text) return 0;
  size_t n = strlen(text);
  if (n == 0 || n > ESL_TEXT_MAX) return 0;
  memset(out, 0, ESL_SLOT_SIZE);
  out[0] = ESL_MAGIC_SLOT;
  out[1] = 0;
  out[2] = (uint8_t)(n & 0xFF);
  out[3] = (uint8_t)((n >> 8) & 0xFF);
  uint16_t crc = eslogCrc16((const uint8_t*)text, n);
  out[4] = (uint8_t)(crc & 0xFF);
  out[5] = (uint8_t)((crc >> 8) & 0xFF);
  out[6] = (uint8_t)(uptime & 0xFF);
  out[7] = (uint8_t)((uptime >> 8) & 0xFF);
  out[8] = (uint8_t)((uptime >> 16) & 0xFF);
  out[9] = (uint8_t)((uptime >> 24) & 0xFF);
  memcpy(out + ESL_SLOT_HDR, text, n);
  return (uint16_t)n;
}

// Validate and decode a slot. Returns true only when magic, length bounds and CRC
// all agree. `out` is always NUL-terminated on success so the text can be printed
// directly; a short `cap` truncates the copy but does not invalidate the record.
static inline bool eslogParseSlot(const uint8_t* slot, char* out, size_t cap,
                                  uint32_t* uptime) {
  if (!slot || !out || cap < 2) return false;
  if (slot[0] != ESL_MAGIC_SLOT) return false;
  uint16_t len = (uint16_t)slot[2] | ((uint16_t)slot[3] << 8);
  if (len == 0 || len > ESL_TEXT_MAX) return false;
  uint16_t crc = (uint16_t)slot[4] | ((uint16_t)slot[5] << 8);
  if (eslogCrc16(slot + ESL_SLOT_HDR, len) != crc) return false;
  size_t n = (len < cap - 1) ? (size_t)len : (cap - 1);
  memcpy(out, slot + ESL_SLOT_HDR, n);
  out[n] = '\0';
  if (uptime) {
    *uptime = (uint32_t)slot[6] | ((uint32_t)slot[7] << 8) |
              ((uint32_t)slot[8] << 16) | ((uint32_t)slot[9] << 24);
  }
  return true;
}

// Encode/decode the 16-byte partition header. Split out so the header can be
// validated by the same host tests as the slots.
static inline void eslogBuildHdr(uint8_t* out, uint32_t head, uint32_t total) {
  memset(out, 0, ESL_HDR_SIZE);
  out[0] = (uint8_t)(ESL_MAGIC_PART & 0xFF);
  out[1] = (uint8_t)((ESL_MAGIC_PART >> 8) & 0xFF);
  out[2] = (uint8_t)((ESL_MAGIC_PART >> 16) & 0xFF);
  out[3] = (uint8_t)((ESL_MAGIC_PART >> 24) & 0xFF);
  out[4] = (uint8_t)(ESL_VERSION & 0xFF);
  out[5] = (uint8_t)((ESL_VERSION >> 8) & 0xFF);
  for (int i = 0; i < 4; i++) out[8 + i]  = (uint8_t)((head  >> (8 * i)) & 0xFF);
  for (int i = 0; i < 4; i++) out[12 + i] = (uint8_t)((total >> (8 * i)) & 0xFF);
}

// Returns false for anything that is not a header this version understands, so a
// blank (0xFF) or foreign partition is treated as "not initialised" rather than
// being read as a ring full of wild indices.
static inline bool eslogParseHdr(const uint8_t* h, uint32_t* head, uint32_t* total) {
  if (!h) return false;
  uint32_t magic = (uint32_t)h[0] | ((uint32_t)h[1] << 8) |
                   ((uint32_t)h[2] << 16) | ((uint32_t)h[3] << 24);
  if (magic != ESL_MAGIC_PART) return false;
  uint16_t ver = (uint16_t)h[4] | ((uint16_t)h[5] << 8);
  if (ver != ESL_VERSION) return false;
  uint32_t hd = 0, tt = 0;
  for (int i = 0; i < 4; i++) hd |= (uint32_t)h[8 + i]  << (8 * i);
  for (int i = 0; i < 4; i++) tt |= (uint32_t)h[12 + i] << (8 * i);
  if (head)  *head  = hd;
  if (total) *total = tt;
  return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Flash I/O (target builds only)
// ─────────────────────────────────────────────────────────────────────────────
// Deliberately thin: all the framing and ring arithmetic lives in the pure
// helpers above, so there is nothing decision-shaped down here to leave untested.
//
// Appends land in a RAM buffer, never in flash. `esLogTick()` (called from
// loop()) flushes, and a full buffer also forces a flush, so the detection path
// pays only a memcpy. A crash therefore costs at most the unflushed buffer.
#ifdef ARDUINO

#include <Arduino.h>
#include <esp_partition.h>

#define ESL_BUF_LEN   1024          // ~12-16 records of text before a flush
#define ESL_PART_NAME "eslog"

static const esp_partition_t* eslPart  = nullptr;
static uint32_t eslCap   = 0;
static uint32_t eslHead  = 0;
static uint32_t eslTotal = 0;
static uint32_t eslLost  = 0;    // records overwritten once the ring wrapped
static uint32_t eslDrop  = 0;    // records refused (text too long / buffer full)
static bool     eslReady = false;
static char     eslBuf[ESL_BUF_LEN];
static size_t   eslBufLen = 0;

static bool eslWriteHdr() {
  uint8_t h[ESL_HDR_SIZE];
  eslogBuildHdr(h, eslHead, eslTotal);
  return esp_partition_write(eslPart, 0, h, ESL_HDR_SIZE) == ESP_OK;
}

// Write out every buffered record, oldest first, then update the header.
//
// ORDER MATTERS: the slot is written before the header that counts it. A power
// loss in between therefore leaves a written-but-uncounted record, which the
// reader never sees — the ring reports *fewer* records than exist. The reverse
// order would leave head/total counting a slot that was never written, i.e. a
// phantom record that reads back as corrupt. Under-reporting is the safer
// failure, and the record was already printed live in any case.
static void esLogFlush() {
  if (!eslReady || eslBufLen == 0) return;
  uint8_t slot[ESL_SLOT_SIZE];
  uint32_t nowS = millis() / 1000UL;
  size_t off = 0;
  while (off < eslBufLen) {
    const char* text = eslBuf + off;
    size_t n = strlen(text);
    off += n + 1;
    if (n == 0) continue;
    if (eslogBuildSlot(slot, text, nowS) == 0) { eslDrop++; continue; }
    if (esp_partition_write(eslPart, eslogSlotOff(eslHead), slot, ESL_SLOT_SIZE) != ESP_OK) {
      eslDrop++;
      continue;
    }
    eslHead = eslogNextSlot(eslHead, eslCap);
    if (eslTotal < 0xFFFFFFFFu) eslTotal++;
    if (eslTotal > eslCap) eslLost = eslTotal - eslCap;
  }
  eslBufLen = 0;
  eslWriteHdr();
}

// Queue one line for the on-device log. Safe to call from loop context (the
// detection funnels); it performs no flash I/O itself.
static void esLogNote(const char* line) {
  if (!eslReady || !line) return;
  size_t n = strlen(line);
  if (n == 0) return;
  // Refuse rather than truncate: a cut-off line may no longer parse back into a
  // detection, which would corrupt a replay silently.
  if (n > ESL_TEXT_MAX) { eslDrop++; return; }
  if (eslBufLen + n + 1 > ESL_BUF_LEN) esLogFlush();
  if (eslBufLen + n + 1 > ESL_BUF_LEN) { eslDrop++; return; }
  memcpy(eslBuf + eslBufLen, line, n);
  eslBufLen += n;
  eslBuf[eslBufLen++] = '\0';
}

static void esLogTick() { esLogFlush(); }

// Count a line the caller could not even format into its buffer. Never stored:
// a truncated line can stop parsing back into a detection, so a refusal is
// visible in the dump counters instead of silently corrupting a replay.
static inline void esLogRefused() { eslDrop++; }

// Stream stored records oldest-first, each preceded by its uptime. The `slog`
// lines match no `api/eyespy.py` pattern (verified), so a dump can be piped
// straight into the existing parser: the records themselves are byte-identical
// to what the device printed live.
static void esLogDump(Stream& out) {
  if (!eslReady) { out.println("[eyespy] slog dump unavailable (no eslog partition)"); return; }
  uint32_t count = eslogCount(eslTotal, eslCap);
  out.printf("[eyespy] slog dump begin count=%lu total=%lu lost=%lu dropped=%lu\n",
             (unsigned long)count, (unsigned long)eslTotal,
             (unsigned long)eslLost, (unsigned long)eslDrop);
  if (count) {
    uint32_t idx = eslogOldest(eslTotal, eslCap, eslHead);
    uint8_t slot[ESL_SLOT_SIZE];
    char text[ESL_TEXT_MAX + 1];
    for (uint32_t i = 0; i < count; i++) {
      if (esp_partition_read(eslPart, eslogSlotOff(idx), slot, ESL_SLOT_SIZE) == ESP_OK) {
        uint32_t up = 0;
        if (eslogParseSlot(slot, text, sizeof(text), &up)) {
          out.printf("[eyespy] slog t=%lus\n", (unsigned long)up);
          out.println(text);
        }
      }
      idx = eslogNextSlot(idx, eslCap);
    }
  }
  out.println("[eyespy] slog dump end");
}

static void esLogClear() {
  if (!eslReady) return;
  // Erase the whole partition, not just the used slots: the erase size must be
  // 4 KB-aligned (a partition is a whole number of 4 KB blocks; a run of slots is
  // not), and anything left behind would read back as garbage.
  eslBufLen = 0;
  eslHead = 0; eslTotal = 0; eslLost = 0; eslDrop = 0;
  if (esp_partition_erase_range(eslPart, 0, eslPart->size) != ESP_OK) {
    eslReady = false;
    Serial.println("[eyespy] slog clear FAILED - on-device log disabled");
    return;
  }
  eslWriteHdr();
}

static void esLogInit() {
  eslPart = esp_partition_find_first((esp_partition_type_t)ESP_PARTITION_TYPE_DATA,
                                     (esp_partition_subtype_t)0x41, ESL_PART_NAME);
  if (!eslPart) {
    // Not fatal: a unit flashed with an older partition table simply has no
    // on-device log, and every other feature keeps working.
    Serial.println("[eyespy] slog: no 'eslog' partition - on-device log disabled");
    return;
  }
  eslCap = eslogCapacity(eslPart->size);
  if (eslCap == 0) {
    Serial.printf("[eyespy] slog: partition too small (%u B) - disabled\n",
                  (unsigned)eslPart->size);
    return;
  }
  uint8_t h[ESL_HDR_SIZE];
  uint32_t head = 0, total = 0;
  bool ok = (esp_partition_read(eslPart, 0, h, ESL_HDR_SIZE) == ESP_OK) &&
            eslogParseHdr(h, &head, &total);
  if (ok && head < eslCap) {
    // Keep the existing ring. Records surviving a reboot is the entire point: a
    // unit that crashes repeatedly leaves a log of everything it saw beforehand.
    eslHead = head; eslTotal = total;
    eslLost = (eslTotal > eslCap) ? (eslTotal - eslCap) : 0;
  } else {
    if (esp_partition_erase_range(eslPart, 0, eslPart->size) != ESP_OK) {
      Serial.println("[eyespy] slog: erase FAILED - on-device log disabled");
      return;
    }
    eslHead = 0; eslTotal = 0; eslLost = 0;
  }
  if (!eslWriteHdr()) {
    Serial.println("[eyespy] slog: header write FAILED - on-device log disabled");
    return;
  }
  eslReady = true;
  Serial.printf("[eyespy] slog ready: %lu slots, %lu stored, %lu lost\n",
                (unsigned long)eslCap, (unsigned long)eslogCount(eslTotal, eslCap),
                (unsigned long)eslLost);
}

static inline bool     esLogReady()   { return eslReady; }
static inline uint32_t esLogStored()  { return eslogCount(eslTotal, eslCap); }
static inline uint32_t esLogLost()    { return eslLost; }
static inline uint32_t esLogDropped() { return eslDrop; }

#else  // host build: the pure helpers above are all the tests need.

static inline void     esLogInit() {}
static inline void     esLogNote(const char*) {}
static inline void     esLogRefused() {}
static inline void     esLogTick() {}
static inline bool     esLogReady()   { return false; }
static inline uint32_t esLogStored()  { return 0; }
static inline uint32_t esLogLost()    { return 0; }
static inline uint32_t esLogDropped() { return 0; }

#endif  // ARDUINO
