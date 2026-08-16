# Test-Before-Commit Rules — eye-spy

This project is embedded firmware for a passive surveillance/tracker
detector. A change that "compiles" is not a change that "works" — the
sibling `flock-you-esp32` project has repeatedly shipped bugs (blocking
calls masquerading as async, radio coexistence hangs, silently-ignored
return values) that only a real build + real hardware test would have
caught, and eye-spy's own `startBLEScan()` blocking-overload bug (see
`01-clean-code.md`) proves the same class of bug applies here too. Follow
this workflow before considering any firmware change complete.

## Minimum bar before calling a fix "done"

1. **Build-verify, not just edit-and-hope.** Use the platformio-mcp
   `build_project` tool (or `pio run -e <env>`) for every environment your
   change plausibly affects. If you touch shared code (`main.cpp`,
   `es_detect.h`, `es_confidence.h`, `ui_task.h`, `led_neopixel.h`,
   display headers), that means representative coverage across the board
   variants in `platformio.ini` — `atom-lite` (no M5Unified, `HAS_SIMPLE_BUTTON`
   path), at least one `m5stack-basic*`/`m5stickc-plus-se*` variant
   (M5Unified display + button path), and both a `-ble` and non-`-ble`
   flavor of whichever variant you're testing where both exist. A change
   that only compiles for one board/config can silently break another.
2. **Also run the native unit tests.** `pio test -e native` (or the
   platformio-mcp `run_tests` tool) runs the Unity host-side tests in
   `test/test_oui_matching/` and `test/test_ssid_ble_matching/` (32 tests
   total) against `es_detect.h`'s pure matching functions. These run in
   seconds on the host with no hardware and should be treated as a fast
   pre-check before any embedded build, especially for changes to
   `es_detect.h`'s pattern tables or matching helpers.
   - Note: `native` is a test-only environment with no `main.cpp` target —
     invoking `build_project`/`pio run` (rather than `run_tests`/`pio
     test`) against it will fail with "Nothing to build" even when
     everything is fine. Don't mistake that for a real regression.
3. **Never trust a build-cache hit after editing source.** If a build
   reports a cache hit immediately after you edited a file it should
   depend on, treat that as suspicious — force a clean rebuild before
   trusting the result.
4. **Watch for build-tool false negatives on `-ble` embedded targets.**
   The platformio-mcp `build_project` diagnostic parser has been observed
   to mis-report a build as failed (`targetStatus: "error"`) even when the
   raw log clearly ends in `Successfully created esp32 image.` and the
   resulting `.pio/build/<env>/firmware.bin` exists and is freshly
   timestamped. If the diagnostic says "error" but no compiler
   error/warning appears anywhere in the raw log and the log's last lines
   show a normal successful esptool image-creation sequence, check the
   actual `firmware.bin` mtime/size before treating it as a real failure.
5. **Hardware-verify claims about runtime behavior.** "This should stop
   the freeze" or "this should fix detection X" are hypotheses, not facts,
   until confirmed via an actual serial capture from a real board.
6. **Capture serial output reliably.** `pio device monitor` cannot run
   non-interactively in most agent environments. Use the PlatformIO
   virtualenv's `pyserial` directly (or the platformio-mcp monitor tools)
   with DTR/RTS both explicitly forced `false` before AND after opening
   the port, since auto-reset boards can otherwise reset unexpectedly
   mid-capture.
7. **Look for silence, not just errors.** A hang/freeze often produces NO
   error message at all — the last clean log line simply never gets a
   successor. When diagnosing a suspected hang (e.g. the `[eyespy] BLE
   scan start` line printing but nothing after it for far longer than
   `BLE_SCAN_DURATION_S`), always capture the FULL boot sequence and check
   where printed output actually stalls relative to the phase it's in.
8. **Don't declare victory on a single successful run.** Radio-based tests
   are inherently a little noisy. Repeat the test at least once, or
   capture a long-enough window, to make sure a fix is robust.

## Before committing

- Re-run `git status`/`git diff --stat` and confirm every changed file is
  intentional — no stray debug prints, no leftover experiment files
  committed by accident.
- Write a commit message that states the *root cause*, not just the
  symptom, especially for hard-to-find bugs (see flock-you-esp32's own
  `git log` for examples of this style, e.g. the `NimBLEScan::start()`
  overload-resolution fix — eye-spy's own `startBLEScan()` fix follows the
  same template).
