# ADR-0003: On-device detection log (crash-resilient and retrievable)

**Status:** Accepted
**Date:** 2026-09-19

## Context

Eye Spy persists **nothing** on device. Every detection, score and tracked device
lives only in RAM, so a reset, a power cut or a crash erases the entire history —
and this is the only record that exists when the unit is run standalone, with no
dashboard attached. (ADR-0001 took the config partition's 4 KB out of `spiffs`
partly *because* this firmware keeps nothing on disk; that premise is what this
ADR revisits.)

Three facts made this urgent:

1. **A field unit was reported rebooting roughly hourly, and the report could not
   be acted on.** The same instrumentation problem existed here: without knowing
   *what the device had seen*, a reboot is a story rather than a diagnosis. That
   work (reset reason + heap + a coredump partition) is already in; this ADR
   covers the missing half — what the device was *doing* before it died.
2. **The sibling project shows what not to do.** `flock-you-esp32` *does* persist
   sessions atomically (tmp → CRC → promote, 60 s autosave), but has **no serial
   command interface and no way to read those files off the device**. It is
   effectively write-only storage. A crash log that cannot be retrieved is worth
   nothing, so retrieval is part of this decision rather than a follow-up.
3. **The log format already exists and is versioned.** The scored detections this
   firmware prints (`[eyespy] +5 (Axon-cam)  score=10`, `[eyespy] <tag>  RSSI=…  #n`)
   are parsed by `api/eyespy.py` and covered by ADR-0002's log schema. Storing the
   same text means a dump can be replayed through the existing parser with **no
   new format, no new decoder, and no API change**.

## Decision

Add a **raw `eslog` partition (64 KB)** holding a ring of fixed-size, self-validating
slots of the firmware's own detection lines, with a serial command to read it back.

1. **Space comes from `app1` only** — `app1` shrinks `0x1F0000 → 0x1E0000` and
   `eslog` takes `0x3E0000`. Every other offset is untouched: `nvs`, `otadata`,
   `app0`, `spiffs`, `coredump` and `escfg` all keep their existing addresses, so
   the flasher's config-part URL (`0x3FF000`) does not move. The OTA slots become
   asymmetric, which is safe here because this firmware has no OTA and the binding
   constraint is the *smaller* slot: 1920 KB against a largest image of 1170 KB.

2. **Fixed 128-byte slots in a ring, each self-validating** —
   `[0xA5][u16 len][u16 crc16][u32 uptime_s][text ≤118]`, after a 16-byte partition
   header (magic, version, head, sequence). Fixed slots mean a torn write during a
   flush invalidates **exactly one** record; the reader validates each slot
   independently and needs no resynchronisation scan. A byte-wise circular buffer
   would have required a resync walk over partially-written text, which is the
   kind of parser this project has already been bitten by.

3. **Buffered writes, never from the detection path.** Records are appended to a
   ~1 KB RAM buffer and flushed from a periodic loop tick or when the buffer
   fills. No flash write ever happens inside `CHECK_DET`/`addScore`, so the
   detection path keeps its timing, and a crash costs at most the unflushed
   buffer.

4. **Contents: the lines that are already the API.** `addScore()` and the
   `CHECK_DET`/`CHECK_TRACKER` funnels build their line once, print it
   byte-identically to today, and hand the same bytes to the log. Because
   `CHECK_DET` is a single funnel, a future engine is captured automatically
   rather than needing to remember. A **boot marker** is written at startup so a
   reboot is visible as a boundary in the log.

5. **Retrieval via serial commands** — `dumplog` streams stored records oldest
   first, each preceded by its uptime; `clearlog` empties the ring. This is the
   firmware's **first inbound serial interface**, so it is deliberately minimal:
   line-buffered, bounded line length, loop context only, unknown input ignored.

## Consequences

- **Retention:** 511 records, oldest overwritten. At roughly 60–90 stored bytes
  per detection that is hours in a busy area and days in a quiet one; the count
  and the overwrite count (`lost=`) are reported on the periodic status line so
  support can tell "the log is running" from "it has been recycling for a week".
- **A dump is replayable.** Feeding `dumplog` output to `api/eyespy.py`
  reconstructs the session on the dashboard, which is what makes this useful to
  someone who was not holding the device.
- **New inbound surface.** A command parser accepts bytes it previously ignored.
  Mitigated by scope: two commands, bounded length, no arguments, loop context.
- **`app1` is now smaller than `app0`.** Recorded above with the reason, because
  it is the kind of asymmetry that invites a "fix" later.
- **Still not a substitute for the dashboard.** Records are the *scored* events;
  raw per-engine OUI/SSID sighting detail is not stored (see below).

## Alternatives considered

- **Compact binary records** (~16 bytes vs ~80): about six times denser, rejected
  because it needs a new record format *and* a host-side decoder, duplicating what
  ADR-0002 already versions — for a diagnostic log whose whole value is being
  readable and replayable.
- **Store into the existing `spiffs` partition:** 12 KB (≈100 records) and typed
  `spiffs` while holding raw records, which would mislead anyone reading the
  table. Rejected.
- **Read the partition offline with `esptool`:** needs a manual extraction step
  and still has to be re-framed by hand, so a dump could not be replayed into the
  dashboard. Rejected — and this is precisely the trap `flock-you-esp32` is in.
- **Survive power loss with no loss at all:** would require writing every
  detection synchronously to flash, putting a flash erase/write on the detection
  path. Rejected: the timing risk outweighs losing a few seconds of log.
- **Also store every raw sighting line (`cam OUI …`, `Flock SSID "…"`):** would
  multiply volume for little diagnostic gain, since scored events already name the
  engine. The hook is in the same funnels, so extending later means adding calls
  at those emitters, not reworking the storage.
