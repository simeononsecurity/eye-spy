# ADR-0002: Version the serial log format

**Status:** Accepted
**Date:** 2026-09-19

## Context

This firmware's serial output is not just a debug log — it is the **API** between
the device and its consumers:

- `api/eyespy.py` is a **text-line parser**: it matches `[eyespy] …` lines with
  regular expressions and turns them into dashboard records, session entries and
  CSV rows. There is no JSON and no schema.
- The printable guides and [`docs/customer-replies.md`](../customer-replies.md)
  tell users what the lines mean, so the format is documented behaviour.
- The beacon tester (`es_beacon_test.cpp`) deliberately mirrors the same field
  names, so it is also a compatibility surface.

Because the parser is pattern-based rather than structural, a formatting change
does not raise an error — the line simply fails to match and is **dropped
silently**. That has now happened three times:

1. **SSID-only lines** (`[eyespy] Flock SSID "…"`, ALPR SSID, cam SSID) were never
   parsed, because every WiFi pattern required the literal word `OUI`.
2. **Padded right-aligned OUI lines** (`[eyespy] ALPR OUI      00:0e:58`) failed,
   because the pattern after `OUI` was a single literal space where the format
   string emits several.
3. **Raven UUID lines** (`[eyespy] Raven UUID <uuid> RSSI=-71`) failed, because
   `_RE_BLE` required two or more spaces before `RSSI=` while that emitter
   printed one. Every Raven detection was invisible to the dashboard, session
   and exports.

In all three cases the firmware was correct, the parser was correct *for the
format it expected*, and only the join between them was wrong — with nothing
logged anywhere.

## Decision

**Put an integer format version in the emitted stream, and treat any change to
the shape or meaning of an existing line as requiring a bump.**

- The boot banner gains `schema=<n>` (currently `1`), so a unit's format is
  identifiable before any detection occurs.
- Any change to an existing line's shape, field order, spacing or meaning bumps
  it; adding a *new, distinct* line does not.
- `api/eyespy.py` warns once when it sees a banner whose schema is newer than the
  parser understands, instead of continuing to drop unrecognised lines silently.

## Consequences

**Gains**

- Version skew becomes visible. Today the failure mode is "the dashboard shows
  nothing and nobody knows why"; with this it is "unit speaks schema 2, this API
  understands 1 — update the API".
- Support can identify a build from a pasted banner, which the banner already
  partly does by printing board, OUI counts and channel mode.
- It formalises what the tests already assume: the log format is a contract, and
  the existing "instantiate every log line and run it through the real parser"
  check (see `.clinerules/02-test-before-commit.md` item 10) is how it is
  enforced.

**Costs**

- Discipline: a version nobody bumps is worse than no version, because it implies
  a guarantee that does not hold. Any change to an existing line must bump it and
  must be noted in `CHANGELOG.md`.
- The version is a **contract, not a mechanism**. The parser stays
  pattern-based, so it is still possible to write a line that matches nothing —
  the version makes that detectable, not impossible. The instantiation test
  remains the real defence.
- The boot banner gains one token, so anything scraping the banner by position
  rather than by name would need updating (nothing currently does).

## Alternatives considered

- **Convert the whole log to JSON** — rejected for now: it would force every
  existing consumer, doc and test to change at once, and the text format is
  readable over a plain USB console, which is the primary way users see it. This
  is worth revisiting as its own ADR if the line count keeps growing.
- **Rely on git history** — rejected: consumers are end users and forks who never
  see it, and the three bugs above prove "obviously compatible" changes are not.
- **Bump only the firmware version** — rejected as insufficient: the same
  firmware version can emit different shapes across a fork, and a consumer needs
  to know what it is *reading*, not what it is talking to.
