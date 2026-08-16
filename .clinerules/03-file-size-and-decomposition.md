# File Size & Decomposition Rules — eye-spy

`main.cpp` is already decomposed along the same lines pioneered by the
sibling `flock-you-esp32` project: pattern tables and pure matching
helpers live in `es_detect.h`, all scoring/decay/persistence-tracking
state and logic lives in `es_confidence.h`, the decoupled FreeRTOS
UI/display task lives in `ui_task.h`, and each board's display driver gets
its own header (`c5_display.h`, `m5basic_display.h`,
`m5stickc_display.h`). Keep that discipline going forward.

## When adding new functionality

- **New, logically-separable functionality gets its own header**, not
  another few hundred lines bolted onto `main.cpp`. Ask: "if I deleted
  this feature entirely, could I remove exactly one `#include` line and
  the file(s) it names, with no scattered remnants?" If not, it isn't
  decomposed enough yet.
- **One responsibility per file.** `es_detect.h` holds pattern tables
  (`FLOCK_OUIS`, `CAM_OUIS`, `RAVEN_UUIDS`, SSID/name keyword lists) and
  pure matching helpers (`ouiMatch()`, `ssidHas()`, `strContainsCI()`) —
  nothing else. `es_confidence.h` owns scoring/decay/persistence
  (`addScore()`, `tickDecay()`, `purgeTracked()`, the `DECL_DETECTOR`/
  `CHECK_DET` macros) — nothing else. `ui_task.h` owns the decoupled
  LED/display/button FreeRTOS task — nothing else. Don't let a detection
  header grow display code, or vice versa.
- **Prefer a new file over a new `#if defined(...)` block that spans
  hundreds of lines inside `main.cpp`.** A handful of `#if`/`#endif` lines
  wrapping a short block (as used throughout `main.cpp`'s board/feature
  selection section) is fine; a multi-hundred-line conditional region is a
  sign the feature should be its own header, included conditionally
  instead (see how `c5_display.h`/`m5basic_display.h`/
  `m5stickc_display.h` are wired in via `USE_C5_DISPLAY`/`USE_M5BASIC`/
  `USE_M5STICKC_PLUS_SE`).
- **A new detector belongs in the existing macro machinery, not a
  one-off.** Adding BLE/WiFi detection engine #23 means: add a
  `DECL_DETECTOR(name)` in `es_confidence.h`, a `PTS_NAME` weight next to
  the other `PTS_*` defines, a `CHECK_DET(name, PTS_NAME, "tag")` line in
  `processBLE()` (or the WiFi-scan equivalent in `processWifiScan()`), and
  update this repo's own detection-engine table (`README.md` and/or
  `.clinerules/04-detection-methods.md` if one exists) — not a bespoke new
  global/flag naming scheme.

## When editing existing large files

- If a change to `main.cpp` doesn't have an obvious existing home in one
  of the included headers, that's a signal — either it belongs in a new
  header, or an existing header's scope should expand to explicitly cover
  it (update that header's own top-of-file comment describing its scope
  when you do).
- Don't grow `platformio.ini` environments by copy-pasting a whole
  environment block for a minor variant. Check whether the difference can
  be expressed as one additional `build_flags` entry on a shared pattern
  instead. Note: as of this writing, the `-DENABLE_BLE_SCAN=1
  -DBLE_COEX_MODE=1` flags on the `*-ble` environment variants
  (`m5stack-basic-ble`, `m5stack-core2-aws-ble`, `m5stickc-plus-se-ble`)
  are **vestigial** — `main.cpp` does not branch on either macro anywhere;
  BLE scanning always runs unconditionally via the phase state machine
  regardless of which environment is built. If a real BLE-coexistence mode
  (continuous BLE scan running concurrently with WiFi, analogous to
  flock-you-esp32's `bleCoexStart()`) is ever implemented here, these
  flags should be wired up to actually gate it; until then, don't assume
  they do anything.
