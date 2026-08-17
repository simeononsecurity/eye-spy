# Keep These Rules Current — eye-spy

This `.clinerules/` folder is meant to be a living record of hard-won
project knowledge, not a static document written once and forgotten. It
was seeded by porting the equivalent, more mature knowledge base from the
sibling `flock-you-esp32` project (same author, closely related
architecture) after eye-spy's own `startBLEScan()` blocking-overload bug
was found to be the exact same bug class flock-you-esp32 had already
documented and fixed. The goal is for each new agent session to start
from where the last one left off, not re-derive the same investigations
from scratch.

## When to update `.clinerules/`

Update the relevant file in this folder (or add a new one) whenever you:

1. **Fix a bug whose root cause wasn't obvious from the code alone** —
   especially anything involving C++ overload resolution, blocking vs.
   async APIs, ISR/callback-context restrictions, or hardware-specific
   quirks. Add a short note to `01-clean-code.md` or create a new topic
   file if the lesson is broadly applicable.
2. **Add, remove, or change the scoring of a detection engine** — keep
   `README.md`'s detection-engine tables and this folder's
   `04-detection-methods.md` in sync with
   `es_detect.h`/`es_confidence.h`/`main.cpp`. Don't let it drift out of
   sync the way flock-you-esp32's own equivalent file warns against.
3. **Add a new test/validation method or tool** — document it in
   `02-test-before-commit.md` so future sessions reuse it instead of
   re-inventing it. eye-spy's hardware-in-the-loop beacon tester
   (`src/es_beacon_test.cpp`, `[env:atom-lite-beacon]`) was added this way
   — see `04-detection-methods.md`'s "Test tooling" section for its full
   scenario list and usage. If it is ever extended (e.g. a new detection
   engine added upstream that the tester doesn't yet cover), update that
   section too.
4. **Discover a new architectural convention worth enforcing** (a new
   naming prefix, a new file-decomposition boundary, a new board-variant
   pattern in `platformio.ini`) — add it to `03-file-size-and-decomposition.md`.
5. **Find that an existing rule in this folder is stale, wrong, or no
   longer applies** (e.g. the `BLE_COEX_MODE`/`ENABLE_BLE_SCAN` flags
   noted as vestigial in `03-file-size-and-decomposition.md` — if a future
   change actually wires them up, that note must be corrected) — correct
   or remove it rather than leaving contradictory guidance behind.

## What NOT to do

- Don't let this folder become a changelog. Rules should describe durable
  facts and conventions, not one-off narration of a specific session's
  blow-by-blow work (that belongs in commit messages / `git log`).
- Don't duplicate information that's better expressed as a code comment at
  the exact call site it concerns. Use `.clinerules/` for cross-cutting
  knowledge a reader wouldn't find by reading one file in isolation.
- Don't add speculative/unverified guidance. Everything here should
  reflect something actually confirmed true about this codebase (via
  hardware testing, careful code reading, or direct experience fixing a
  real bug) — see `02-test-before-commit.md`'s standard for what counts as
  "confirmed."
