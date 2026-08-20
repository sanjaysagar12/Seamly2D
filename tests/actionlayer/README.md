# actionlayer tests

This directory is the integration-test harness for `seamly2d-actiond` (`src/app/actiond`), the
headless JSON action-layer daemon (`src/libs/actionlayer`). It runs `actiond` as a real subprocess
against a fixture pattern, diffs its JSON response against a committed golden file, and checks that
any rendered PNG actually got produced. There is also a separate C++/QtTest unit suite at
`src/test/ActionLayerTest/` that exercises `ActionEngine`/`NameResolver`/etc. directly at the C++
level rather than through the `actiond` CLI -- see that directory for tests below the process
boundary this harness stays above.

## Layout

```
tests/actionlayer/
    fixtures/
        patterns/
            minimal.val   -- smallest valid pattern: one draft block ("Front"), one point (A).
            blank.val     -- schema-minimum empty pattern (a placeholder "_seed" draft block with
                             no points) -- every scripts/*.json case below builds its own geometry
                             from scratch against this fixture.
        measurements/
            sample.smis   -- a handful of realistic individual measurements (height, bust_circ,
                             shoulder_length, ...) -- used to prove formula fields stay
                             measurement-reactive (see scripts/03_formula_points.json).
    scripts/
        01_dump_only.json            -- read-only pattern.dump against an empty pattern.
        02_basepoint_and_line.json   -- baseline: basePoint + endLine + line + dump + render.
        03_formula_points.json       -- endLine/normal/alongLine, literal AND measurement formulas.
        04_curves.json               -- arc + spline.
        05_operations.json           -- move/rotation/mirrorByAxis/group.
        06_piece_and_union.json      -- piece.addPatternPiece/addAnchorPoint/internalPath (piece.union
                                        deliberately excluded -- see "Known gaps" below).
        07_error_cases.json          -- malformed formula, unknown name, unknown op: proves each
                                        fails cleanly and the batch keeps running afterward.
    expected/
        <case>.expected.json   -- golden JSON response for scripts/<case>.json.
        <case>.png              -- golden reference render, only for cases with a render.snapshot
                                    action (visual reference for human/AI review -- see "Interpreting
                                    a failure" below; never byte-diffed).
    run_batch/
        run_batch.pro   -- qmake project (QT += core only; no Seamly2D libs).
        main.cpp         -- the harness driver; see its own module header comment for full detail.
    output/   -- gitignored scratch directory run_batch writes actual results into.
```

## Prerequisites

- Qt 6.10.3 (msvc2022_64) and a matching MSVC toolchain (this repo's own `build_actiond.bat`/
  `build_and_run.bat` at the repo root hardcode these paths -- edit them if your install differs).
- Build the whole tree once from the repo root (creates `out/`):
  ```
  build_actiond.bat
  ```
  This builds `actiond.exe` (`out/src/app/actiond/bin/`) and, via one top-level `qmake` pass,
  regenerates every other project's Makefile too (including this harness's), but does **not**
  compile them -- see "Building just run_batch" below for that.
- `actiond.exe` must exist before running any test in this directory. `run_batch` looks for it at
  `$ACTIOND_EXE` first, then walks up from its own binary's directory to find an ancestor
  literally named `out`, then descends into `src/app/actiond/bin/actiond(.exe)` from there -- so it
  finds the right binary regardless of exactly where qmake happened to place `run_batch`'s own
  build output, as long as both were built under the same `out/` tree.

### Building just `run_batch`

```
cd out\tests\actionlayer\run_batch
qmake ..\..\..\..\tests\actionlayer\run_batch\run_batch.pro
nmake
```
(On first run, `qmake Seamly2D.pro` from the repo root must have already run at least once --
`build_actiond.bat` does this -- so `run_batch.pro`'s own dependency on `common.pri` resolves.)
The binary lands at `out/tests/actionlayer/run_batch/bin/run_batch.exe`. Make sure
`C:\Qt\6.10.3\msvc2022_64\bin` is on `PATH` before running it directly (Qt6Core.dll etc.) --
`build_actiond.bat`/the `nmake check` target both set this up for you already.

## Running one test

```
out\tests\actionlayer\run_batch\bin\run_batch.exe tests\actionlayer\scripts\02_basepoint_and_line.json
```
Real output:
```
[02_basepoint_and_line] PASS
```
On failure it instead prints every JSON path that differs and where to look:
```
[02_basepoint_and_line] FAIL (2 mismatch(es)):
    $.results[1].value.id: expected [2], got [3]
    $.results[1].value.name: expected ["B"], got ["C"]
    actual response: C:/.../tests/actionlayer/output/02_basepoint_and_line/response.json
```
A bare actions-file argument uses the default fixtures (`fixtures/patterns/blank.val` +
`fixtures/measurements/sample.smis`). To use different fixtures, pass them in any order --
arguments are classified by extension, not position:
```
run_batch.exe fixtures\patterns\minimal.val fixtures\measurements\sample.smis scripts\02_basepoint_and_line.json
```
Each case's actual output (the JSON response, `actiond`'s stderr log, the saved `.val`, and any
rendered PNG) lands in `output/<case-name>/` (gitignored) regardless of pass/fail, so a failure can
always be inspected after the fact.

## Running the full suite

```
out\tests\actionlayer\run_batch\bin\run_batch.exe
```
(no arguments) runs every `scripts/*.json` case against the default fixtures and prints a summary:
```
[01_dump_only] PASS
[02_basepoint_and_line] PASS
...
7/7 case(s) passed.
```
Exit code 0 only if every case passed. This is also what `make check` runs (`run_batch.pro` sets
`CONFIG += testcase`, wired into `src/test/test.pro`'s `SUBDIRS` as `ActionLayerBatchTests`,
alongside `ParserTest`/`ActionLayerTest`/etc.) -- from `out/tests/actionlayer/run_batch`:
```
nmake check
```
**Known environment quirk:** on this development machine, `nmake check`'s generated
`target_wrapper.bat` fails with `'<binary>.exe' is not recognized as an internal or external
command` for every `CONFIG += testcase` target in this repo, not just this one -- reproduced
identically against the pre-existing `ActionLayerTest` (confirmed while building this harness, 20
Aug 2026). It is a machine-local `cmd.exe` current-directory executable-resolution setting, not a
defect in this harness's `.pro` wiring. Running the built binary directly (as shown above) is
unaffected and is the reliable way to run the suite on a machine with this quirk; on a machine
without it, `nmake check` should work identically to every other test target here.

## Adding a new test case

1. Add `scripts/NN_description.json` (`{"_description": "...", "actions": [...]}` -- the
   `_description` key is ignored by the engine, purely documentation). Prefer starting from
   `fixtures/patterns/blank.val` (the no-args suite run always does) and building whatever geometry
   the case needs from scratch, so it stays independent of every other case's output.
2. Generate its golden file:
   ```
   run_batch.exe scripts\NN_description.json --update
   ```
3. **Review the diff by hand before committing.** `--update` records whatever the current code
   happens to produce as "correct" -- it has no way to know whether that output is actually right.
   Read `expected/NN_description.expected.json` (and open `expected/NN_description.png` if the case
   renders) and confirm the geometry/errors are what you actually intended, the same way you'd
   review any other generated file before committing it.
4. If the case fails while you're still developing it and you want to update the golden file with
   the new output, rerun `--update` and re-review -- there is no partial/silent update path.

## Interpreting a failure

`run_batch` diffs the whole JSON response structurally (object key order never matters; array
order/length does, since a script's actions run in a fixed order) and prints every differing path
it finds (up to 20). A failure is either:
- **A real regression** -- something in `src/libs/actionlayer` or `src/app/actiond` changed
  behavior unintentionally. Fix the code, rerun without `--update`, confirm it passes again.
- **An intentional behavior change** -- you changed a handler on purpose and its output is
  supposed to differ now. Rerun with `--update`, then review the new golden file by hand (step 3
  above) before committing it.

Rendered PNGs (`expected/<case>.png`) are **never byte-diffed** -- PNG encoding is not guaranteed
byte-stable across Qt/platform versions the way the JSON comparison is. `run_batch` only checks
that a case which is supposed to render actually produced a PNG; open `output/<case>/*.png` next to
`expected/<case>.png` and eyeball them for an actual visual regression.

## Multi-action scripts

Every file under `scripts/` is a **single request containing an array of multiple actions**, run
as one batch against one loaded pattern (`{"actions": [{"op": "basePoint", ...}, {"op": "endLine",
...}, ...]}`) -- this is the primary way this harness tests realistic multi-step construction
sequences, not just individual tool calls in isolation. `scripts/03_formula_points.json` is a good
example: it chains `basePoint` -> two `endLine`s (one with a literal formula, one with a
measurement-name formula) -> `normal` -> `alongLine` -> two `line`s -> `pattern.dump` ->
`render.snapshot`, all against the same in-memory pattern state, exactly the way a real AI-driven
construction session would.

## Known gaps

- **`piece.union` is not exercised by `06_piece_and_union.json`.** It reproducibly segfaults inside
  `VToolUnion` (see `docs/action-layer-schema.md`'s `piece.union` entry and the `KNOWN GAP` comment
  on `handlePieceUnion()` in `src/libs/actionlayer/handlers/piece_handlers.h`). Calling it from a
  script wired into `make check` would crash the whole test run instead of failing one case
  cleanly, so it is a deliberate, documented coverage gap -- remove the `_note` field in that
  script and add real `piece.union` coverage once the crash is root-caused and fixed.
- **`pattern.dump`'s object ordering used to be non-deterministic.** Found while building this
  harness (20 Aug 2026): `pattern.dump` walked `VContainer::DataGObjects()` (a `QHash`) directly,
  and `QHash`'s iteration order is randomized per process (Qt's hash-flooding mitigation) -- so the
  identical script run twice against the identical fixture produced its `"objects"` array in two
  different orders, which made golden-file diffing this harness depends on fundamentally
  unreliable. **Fixed** in `src/libs/actionlayer/handlers/pattern_dump_handler.cpp`: the objects are
  now sorted by id before being serialized. Verified fixed by running every case in this suite
  twice in a row and confirming byte-identical `response.json` output both times.
