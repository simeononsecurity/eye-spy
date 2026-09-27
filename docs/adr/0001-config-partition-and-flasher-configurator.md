# ADR-0001: Runtime config partition and web-flasher configurator

**Status:** Accepted — implemented (firmware + web flasher)
**Date:** 2026-09-19
**Supersedes:** none

## Context

Users want to choose, before flashing, **which detections are active**, tune
**sensitivity**, and enable or disable each **output** (LED, chirp/buzzer,
vibration, on-screen hold). Today every one of those knobs is a compile-time
constant — this firmware has no runtime settings layer at all (`Preferences`
and `nvs` appear nowhere in `src/`) — so a change means a maintainer edits
`es_confidence.h`/`es_detect.h`, rebuilds, and the user reflashes.

The constraints that shape the answer:

- **No combinatorial variants.** There are 12 firmware environments, ~21
  detection engines with independent enable/disable, and several output toggles.
  Pre-building combinations is not viable (2^N) and would fragment both CI and
  the flasher.
- **The flasher is a static site.** `docs/index.html` is served from GitHub Pages
  and flashes via **ESP Web Tools** (`esp-web-install-button`). No server exists
  to compile or personalise a build, and we want to keep it that way.
- **ESP Web Tools manifests accept arbitrary parts.** A manifest's `parts` array
  is `{offset, file}` pairs, so a binary can be written to an arbitrary flash
  offset alongside the firmware; the manifest can also be generated in-page and
  handed to the button as an object URL.
- **The partition table is full.** The 4 MB table ends exactly at `0x400000`
  (`spiffs` occupies `0x3F0000`–`0x400000`) and there is a second OTA slot, so
  there is no free space to claim without moving something.
- **Boards differ in what they can even do.** The Atom Lite has no speaker or
  buzzer at all, only Core2 For AWS has a vibration motor, and only some boards
  have a display. A configurator that offers "enable sound" on a board with no
  audio hardware is offering a no-op.

## Decision

**Add a dedicated `escfg` data partition, hold configuration in a fixed-layout
struct protected by a magic number and a CRC32, and have the flasher generate
that blob from the user's choices and flash it as an extra manifest part.**

1. **Storage: a new `escfg` partition** (data type, custom subtype, fixed
   offset, 0x1000 = 4 KB), read once at boot with `esp_partition_read()` — no
   filesystem, no NVS page format, nothing to corrupt into an unreadable state.
   To make room, shrink `spiffs` from `0x10000` to `0xF000` and place `escfg` at
   `0x3FF000`. `nvs`, `otadata`, `app0` and `app1` offsets are untouched, so
   existing OTA slots stay valid.
2. **Layout:** `magic | version | length | payload | crc32(payload)`. A blank
   (freshly erased `= 0xFF`) or bad-CRC partition is **not an error** — it means
   "no user config" and the firmware falls back to the compile-time defaults that
   represent today's behaviour. A plain flash therefore behaves exactly as it
   does now, and a corrupt config degrades to defaults rather than to something
   undefined.
3. **The flasher builds the blob in JS** from checkbox/slider state and flashes
   it as an extra part, and can later rewrite *only* that partition to change
   settings without touching the firmware.
4. **The schema is versioned.** Firmware seeing a newer version than it knows
   ignores the blob and uses defaults; it must never reinterpret bytes it does
   not understand. Same rule as ADR-0002.
5. **Sensitivity is exposed as presets and bounded ranges, never free-form
   weights.** The scoring tiers are load-bearing: this project has three times
   seen a scoring change lift a deliberately-quiet signal over the alert
   threshold, in every case presenting as a status LED stuck red (the
   contract-manufacturer wildcard probe, the IE-fingerprint bonus, and the Raven
   UUID range). The configurator may offer per-engine on/off and
   quieter/default/louder presets; the firmware clamps anything out of range
   rather than trusting the blob.
6. **Options are gated by the selected board's capabilities.** The flasher knows
   which board each manifest targets, so "sound" is only offered where a speaker
   or buzzer exists and "vibration" only on Core2. This avoids shipping a UI that
   silently promises something the hardware cannot do — the same mistake that
   produced the "Eye Spy is silent" documentation bug.

## Consequences

**Gains**

- One firmware per board and the user's choices are theirs — no maintainer
  rebuild, no variant explosion, no CI matrix growth.
- Settings survive a firmware reflash (separate partition), so an update does not
  silently reset someone's preferences.
- The same mechanism later allows changing settings without reflashing the
  firmware at all, because writing one 4 KB partition is quick.
- CRC32 bootstraps session integrity: the same envelope should protect any
  detection log we persist, which today has **no** integrity check at all.
- It finally gives this firmware a settings layer. There is none today, so every
  user-visible behaviour is frozen at build time — the single biggest
  inflexibility in the project.

**Costs / risks**

- **A partition-table change.** `spiffs` shrinks `0x10000` → `0xF000` to make
  room. `nvs`/`otadata`/`app0`/`app1` offsets are preserved, but anyone updating
  from the old table must full-flash (the web flasher does).
- The boot path gains code that must never be able to stop the detector from
  starting: a failed config read must be treated as "no config" and log a line,
  not abort. This is the silent-failure class this project has been bitten by
  repeatedly, so it gets explicit tests.
- A user can make the device *less* sensitive and then report missed detections.
  Mitigation: presets rather than raw numbers, a clear default, and a boot-banner
  line stating whether a user config was loaded — so support can distinguish
  "user dialled it down" from "broken".
- Two sources of truth for behaviour (compile-time defaults and runtime config).
  Mitigation: defaults live in exactly one place and the config layer may only
  *override*, never redefine.
- Per-board capability gating means the flasher must know each board's outputs.
  That information exists in `src/main.cpp`'s board blocks, so it will need to be
  restated in the flasher's board table — a drift risk to keep in mind (the
  printable guides already duplicate some of it).

## Alternatives considered

- **Pre-built firmware variants per combination** — rejected: combinatorial, and
  it moves the choice from the user's hands into our build system.
- **Writing an NVS image from the flasher** — rejected: NVS requires generating
  pages with its own entry format and CRC semantics client-side; far more fragile
  than a flat struct for no benefit.
- **A LittleFS/SPIFFS config file** — rejected: needs a filesystem image and a
  mount on the boot path to store a few dozen bytes.
- **Runtime settings over serial, as flock-you-esp32's dashboard does** —
  rejected as the *primary* mechanism: it needs a host running the dashboard, so
  a user who just flashed from the web gets nothing. Worth adding later *on top
  of* this, not instead of it.
- **A phone app** — rejected: none exists for Eye Spy, so configuration would be
  unavailable until one does.

## Verification plan

1. Host unit tests for the codec: round-trip, bad magic, bad CRC, truncated
   payload, unknown-newer version, and bounds clamping of every field.
2. Boot with the partition erased (defaults used, behaviour unchanged) and with a
   valid config (values applied), confirmed via the boot banner.
3. Per-engine disable test: with one engine disabled, that engine must stop
   firing while others continue — measured with the beacon tester against a
   second board, using the existing serial lines.
4. Output toggles verified on the boards that have each output, specifically
   confirming a board *without* audio is unaffected by the sound setting.
5. Firmware reflash over an existing `escfg` preserves settings.


## Implementation notes (firmware side, as built)

- `escfg` took its 4 KB from `spiffs` (`0x10000` → `0xF000`) as budgeted, and
  nothing is lost: this firmware persists nothing on-device. `nvs`, `otadata`,
  `app0` and `app1` offsets are untouched, so existing OTA slots stay valid.
- **Both gates are "clear the flag before anything downstream sees it"**, one per
  protocol, rather than a check inside each engine:
  - Wi-Fi: after the scan loop in `processWifiScan()`, before any `addScore()` or
    `Serial.printf()`. One table-driven block replaces nine separate edits.
  - Bluetooth: at the top of `processBLE()`, before `CHECK_DET`/`CHECK_TRACKER`.
  That placement is what makes "disabled" mean genuinely silent — no score, no
  log line, no on-screen entry, no dashboard record — rather than merely
  unscored.
- The threshold is a *minimum score*, and the configured value can only be
  `>= SCORE_ALERT`. It therefore makes the device quieter but can never let a
  +5-tier signal alert on its own, which is the trap this project has hit three
  times (each presenting as a stuck-red LED). The floor is clamped on decode.
- The tracker-follow window became a parameter
  (`trackerFollowUpdate(state, now, followMs)`), defaulted to the compile-time
  value so existing callers and the 15 tracker tests are unaffected. The firmware
  passes the user's configured minutes; keeping it a parameter rather than reading
  the config inside `es_detect.h` preserves that header's host-testability.
- `m5basicVibrationStop()` was added because skipping the vibration *tick* would
  leave the motor energised mid-pulse. The startup pulses and the arming path are
  gated too, and the config loads at the top of `setup()` so a "no vibration"
  choice is honoured from boot.
