# actionlayer Phase 5 tests -- mutating actions (`basePoint`, `line`)

## Running

```sh
./run_action_tests.sh
```

By default it looks for `actiond.exe` at `../../out/src/app/actiond/bin/actiond.exe` (the qmake
out-of-source build layout `build_actiond.bat` produces). Override with:

```sh
ACTIOND_EXE=/path/to/actiond.exe ./run_action_tests.sh
```

Requires `actiond` to already be built (see `build_actiond.bat` at the repo root). The script
itself builds nothing.

For each `scripts/*.json` file it runs `actiond --pattern fixtures/empty_pattern.val
--measurements fixtures/empty_measurements.smis --actions scripts/<name>.json --save-pattern
output/<name>.val`, against the *same* unmodified fixture every time -- `actiond` never mutates
its `--pattern` input, only what `--save-pattern` names, so runs never leak state into each other.

- If `expected/<name>.json` exists, the actual JSON response is structurally diffed against it
  (via `jq` if present, else `python3`, else an exact-text fallback) and PASS/FAIL is reported.
- Otherwise the response is just printed for manual inspection.
- Every run's resulting pattern is saved to `output/<name>.val` regardless, for inspection.
- Exit code is non-zero if any script crashed (a signal-killed process, exit code >2) or any
  scripted comparison failed; 0 if every script ran to completion (whether individual actions
  inside it succeeded or reported a clean, structured error).

## What each script exercises

- **001_single_basepoint.json** -- one `basePoint` action. No `expected/*.json`; smoke-tests the
  simplest possible mutation.
- **002_basepoint_and_line.json** -- two `basePoint` actions (different draft block names -- see
  "Why two draft blocks?" below) plus one `line` connecting the two resulting points. Has a
  matching `expected/002_basepoint_and_line.json`.
- **003_multi_action_chain.json** -- three `basePoint` actions + two `line` actions in one batch,
  exercising sequential id allocation (ids 1-5, in order) and multi-step name resolution across
  more than two points. Has a matching `expected/003_multi_action_chain.json`.
- **004_error_cases.json** -- three actions: a `basePoint` that succeeds, a second `basePoint`
  that fails because it reuses an *already-existing* draft block name (`"draftBlockExists"`
  error), and a `line` referencing a name that doesn't exist (`"nameResolution"` error, the same
  structured shape Phase 4's `pattern.resolveName` uses). No `expected/*.json`; demonstrates that
  a batch keeps running and returns clean JSON errors after a failure, not a crash.

### `expected/*.json` id assumption

Object ids come from `VContainer`'s process-wide static counter, which starts at 0 and is
pre-incremented on first use (`getNextId()`), so the first object created in a fresh `actiond`
process gets id 1, the second gets id 2, and so on. Since each script run is its own fresh
process reading the same never-mutated fixture, this is deterministic across runs -- the
`expected/*.json` files assume ids are assigned 1, 2, 3, ... in action order within each script,
verified against `actiond`'s actual output before being checked in.

### Why two draft blocks in 002 (not one)?

A `VToolBasePoint` is Seamly2D's model of "the point that anchors a brand-new pattern piece" --
`VToolBasePoint::AddToFile()` unconditionally appends a new `<draftBlock>` element; there is no
"add a second base point to an already-existing draft block" operation in this codebase (a draft
block has exactly one base point, by construction). So `basePoint`'s `"draftBlock"` field names
the *new* block being created, and fails with a `"draftBlockExists"` error if that name is
already taken -- see `handleBasePoint()` in
`src/libs/actionlayer/handlers/point_handlers.cpp` for the full reasoning. `line` has no such
restriction: it connects any two already-named points in the container regardless of which draft
block created them, exactly as 002 and 003 do.

## Fixtures

- `fixtures/empty_pattern.val` -- the current pattern schema requires at least one `<draftBlock>`
  element (`draftBlock+` in the XSD content model) even in an otherwise-empty file, so this
  fixture carries one placeholder block named `_seed` that no test script ever reuses. Every
  `basePoint` action in every script is still exercising real "create a brand-new draft block"
  behavior, not "reuse an existing empty one". Declares `<unit>mm</unit>` deliberately, so
  `qApp->toPixel()`-based conversions inside Seamly2D's own reused code agree with this test
  suite's "JSON coordinates default to mm" convention (see `point_handlers.cpp`).
- `fixtures/empty_measurements.smis` -- a minimal, valid, empty measurements file (zero `<m>`
  entries). `actiond` requires a measurements file to be loadable even when, as here, no formula
  in the script ever references one.
