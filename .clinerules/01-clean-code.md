# Clean Code Rules — eye-spy

These rules apply to all C/C++ firmware code (`src/*.cpp`, `src/*.h`) in
this repository. They mirror the equivalent rules in the sibling
`flock-you-esp32` project, adapted to eye-spy's own architecture
(score-based threat model, `es*` naming prefix, phase-based scan loop).

## General

- **Comment the "why", not just the "what".** When you fix a non-obvious
  bug — wrong C++ overload resolution, a blocking call disguised as an
  async one, an ISR/callback-context restriction, a radio-coexistence
  quirk — leave a comment at the fix site explaining the root cause and
  symptom, not just what changed. Future agents (and humans) must be able
  to understand *why* code looks the way it does without re-deriving the
  investigation from scratch.
- **Never silently swallow an error return value.** If a function returns
  `bool`/an error code and that return is ignored, and the failure mode is
  not obviously harmless, log it.
- **Prefer explicit over implicit in ambiguous API calls.** When a C++ API
  has multiple overloads that could plausibly be selected by argument
  types (e.g. `NimBLEScan::start(uint32_t, bool)` — blocking — vs.
  `start(uint32_t, callback, bool)` — async), make the call unambiguous by
  passing an explicit typed value/cast rather than relying on
  default-argument or implicit-conversion resolution. This exact class of
  bug was found and fixed in `startBLEScan()` (`src/main.cpp`): a bare
  `g_pScan->start(BLE_SCAN_DURATION_S, false)` resolved to NimBLEScan's
  **blocking** 2-arg overload (`ulTaskNotifyTake(pdTRUE, portMAX_DELAY)`
  internally), freezing `loop()` — and therefore button handling and phase
  transitions — for the full `BLE_SCAN_DURATION_S` (9 s) every BLE phase.
  Fixed by passing an explicit typed null callback,
  `g_pScan->start((uint32_t)BLE_SCAN_DURATION_S, (void (*)(NimBLEScanResults))nullptr, false)`,
  to force selection of the async 3-arg overload. This is the identical
  root cause (and identical fix pattern) as flock-you-esp32's
  `bleCoexStart()` bug — see that project's `.clinerules/01-clean-code.md`
  and `ble_selftest.h` for the original investigation.
- **No magic numbers without a named `#define`/`constexpr`.** Timing
  constants, thresholds, RSSI cutoffs, per-engine score weights, etc. must
  be named constants at the top of the relevant section (`main.cpp`'s
  Tuning block, or the `PTS_*`/`SCORE_*` defines in `es_confidence.h`),
  not inline literals.
- **Match existing naming conventions.** Use the `es*` prefix for
  detection-pipeline functions/data (`es_detect.h`, `es_confidence.h`),
  matching flock-you-esp32's own `fy*` convention. Global detection-state
  variables follow the `g_<name>Det`/`g_<name>Rssi`/`g_<name>Seen`/
  `g_<name>Scored` pattern established by `DECL_DETECTOR()` in
  `es_confidence.h` — don't invent a new pattern for a new detector.
- **Keep ISR/callback-context code minimal.** `wifiSniffer()` (`IRAM_ATTR`,
  WiFi promiscuous callback) and `EyeSpyBLECallbacks::onResult()` (NimBLE
  host-task callback) must avoid `Serial.print`, dynamic allocation, and
  anything not safe from an interrupt/foreign-task context. Set the
  lightweight `g_<name>Det/Rssi/Seen` flags and let `processBLE()`'s
  `CHECK_DET()` macro (running from the main `loop()` task) do any
  heavier work (scoring, logging).
- **Only the UI task touches M5Unified/display objects.** Per
  `ui_task.h`'s own documented contract, `M5.update()`, button reads,
  vibration, and all display draws happen exclusively on the dedicated UI
  FreeRTOS task — never from `loop()` or scan/callback code. Communicate
  state to it only through `uiPublish()`/`uiTakeButtonAction()`'s
  critical-section-protected snapshot, not direct calls.

## Reviewing your own changes

Before considering a change complete, re-read the diff and ask:
1. Does every changed line have an obvious reason to exist?
2. Would a future engineer understand *why*, not just *what*, from reading
   the surrounding comments?
3. Did I leave any debug-only logging in that should be removed or gated?
4. If I touched a NimBLE/WiFi API call, did I double check which overload
   it actually resolves to (see the `startBLEScan()` bug above)?
