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
   **Excluding `lilygo-t-dongle-c5`:** that environment currently cannot build
   at all on this project's pinned toolchain, and its failure is not a
   regression. `platformio.ini` uses `espressif32@6.7.0` (Arduino core
   2.0.16), which ships no `platformio-build-esp32c5.py` SConscript, so the
   build dies in ~0.2 s with `*** missing SConscript file
   .../framework-arduinoespressif32/tools/platformio-build-esp32c5.py`. The
   ESP32-C5 requires Arduino core 3.x — which is why the sibling
   flock-you-esp32 project's equivalent C5 environment is pinned to the
   pioarduino `platform-espressif32` release instead. CI builds this env as a
   separate `continue-on-error: true` step so it never fails the workflow.
   Treat a C5 build failure as expected until this env is migrated to a
   core-3.x platform.
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
6. **Prefer a two-board cross-device test over same-device loopback**
   whenever a change touches a detection engine. Flash
   `src/es_beacon_test.cpp` (`[env:atom-lite-beacon]`) to one Atom Lite —
   it rotates through all 21 detection scenarios (12 BLE + 8 WiFi-AP + 1
   WiFi-promiscuous) plus a periodic `persist` scenario, holding each for
   `SCENARIO_HOLD_MS` (20 s) to reliably overlap the detector's ~17-19s
   phase-cycling window — then flash the real detector firmware being
   tested to a second board and confirm each expected engine fires. See
   `04-detection-methods.md`'s "Test tooling" section for the full
   scenario/engine mapping. This firmware only broadcasts test signals —
   never flash it to a device meant to be a real detector.
7. **Capture serial output reliably.** `pio device monitor` cannot run
   non-interactively in most agent environments. Use the PlatformIO
   virtualenv's `pyserial` directly (or the platformio-mcp monitor tools)
   with DTR/RTS both explicitly forced `false` before AND after opening
   the port, since auto-reset boards can otherwise reset unexpectedly
   mid-capture.
8. **Look for silence, not just errors.** A hang/freeze often produces NO
   error message at all — the last clean log line simply never gets a
   successor. When diagnosing a suspected hang (e.g. the `[eyespy] BLE
   scan start` line printing but nothing after it for far longer than
   `BLE_SCAN_DURATION_S`), always capture the FULL boot sequence and check
   where printed output actually stalls relative to the phase it's in.
9. **Don't declare victory on a single successful run.** Radio-based tests
   are inherently a little noisy. Repeat the test at least once, or
   capture a long-enough window, to make sure a fix is robust.
10. **When you add or change a firmware `Serial.printf()` detection line,
   verify the dashboard API can actually parse it.** `api/eyespy.py` is a
   *text-line* parser (`_RE_*` regexes), so any line it doesn't recognise is
   silently dropped — the detection never reaches the dashboard, the
   session, or any export, and nothing anywhere reports an error. This has
   already bitten twice: every SSID-only line (`[eyespy] Flock SSID "…"`,
   ALPR SSID, cam SSID) went unparsed because the regexes only handled
   `… OUI <mac> "…"` forms, and the padded right-aligned OUI lines
   (`[eyespy] ALPR OUI      00:0e:58`) failed because the pattern after
   `OUI` was a single literal space. Verification method that found both:
   instantiate *every* `Serial.printf("[eyespy] …")` format string from
   `src/` with plausible values, run them through `parse_eyespy_line()`, and
   assert that every detection-producing line returns a dict (only
   status / decay / `WiFi done` / boot lines may legitimately return
   `None`). Re-run that check whenever the log format changes.

11. **`[env:lilygo-t-dongle-c5]` cannot be built in this repo at all — verify
   `src/c5_display.h` changes another way.** The env pins `espressif32@6.7.0`,
   whose Arduino core (2.0.16) ships **no** `platformio-build-esp32c5.py`, so
   the build dies in well under a second with
   `*** missing SConscript file '…/platformio-build-esp32c5.py'` and compiles
   **zero** files. That is structural, not transient — it does not fix itself
   on retry, and it means any edit to `c5_display.h` would otherwise ship
   completely unverified (contrast flock-you-esp32, whose C5 envs use the
   `pioarduino/platform-espressif32` fork and do build). Until this repo
   migrates that env, typecheck the header on the host against stub Adafruit
   headers:

   ```sh
   mkdir -p /tmp/c5stub && cd /tmp/c5stub   # write Adafruit_GFX.h,
   # Adafruit_ST7735.h, Adafruit_NeoPixel.h, Arduino.h stubs
   cat > t.cpp <<'EOF'
   #include "Arduino.h"
   #define USE_C5_DISPLAY 1
   #include "c5_display.h"
   int main(){ /* call EVERY function in the header, both branches */ }
   EOF
   g++ -std=gnu++17 -Wall -Wextra -Wno-unused-parameter \
       -I/tmp/c5stub -I<repo>/src -fsyntax-only t.cpp
   ```

   Call every public function in the header (including default-argument and
   null-pointer paths) or `-Wall` will report them as unused and you will not
   actually be typechecking them. This caught nothing in the change that
   introduced it, but it is the only verification available for that board —
   do not skip it on the grounds that "the C5 is experimental".

   **Do not** report the C5 env's failure as a regression introduced by a
   change: check whether any file was compiled (`grep -c Compiling <log>`
   returns 0) before attributing a break to source.

## Before committing

- Re-run `git status`/`git diff --stat` and confirm every changed file is
  intentional — no stray debug prints, no leftover experiment files
  committed by accident.
- Write a commit message that states the *root cause*, not just the
  symptom, especially for hard-to-find bugs (see flock-you-esp32's own
  `git log` for examples of this style, e.g. the `NimBLEScan::start()`
  overload-resolution fix — eye-spy's own `startBLEScan()` fix follows the
  same template).
- **Re-generate the printable guides if you touch `docs/print/`.**
  `docs/print/quick-start-*.pdf` are committed artifacts built from
  `docs/print/make_quick_start_pdfs.py`, so editing the guide text without
  re-running the generator leaves the PDFs stale — and the PDFs are the copy
  non-technical users actually read. Run
  `python3 docs/print/make_quick_start_pdfs.py` after any edit there.
  Add `--check` to measure only: each page's content height is compared with
  its fixed card height and an overflowing page exits non-zero. That gate
  exists because `.page` uses `overflow: hidden`, so over-long text is
  clipped in the PDF with no error anywhere — the same class of silent
  failure as the dropped-detection parser bug above.
