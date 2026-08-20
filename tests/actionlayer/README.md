# actionlayer tests

This directory holds every test harness written against `seamly2d-actiond`
(`src/app/actiond`), the headless JSON action-layer daemon, across every implementation phase.

20 Aug, 2026: `actiond` gained a second mode -- a **persistent NDJSON daemon** (see
`docs/action-layer-schema.md`'s "Session protocol" section), alongside the original one-shot
`--actions <file>` mode every harness below this point was written against. **`scripts/` +
`run_tests.py`** (described first, right below) is the harness for that new daemon mode, and is
also the first harness to exercise `point.edit` -- the edit-in-place action that closes the "no
edit-in-place or delete action exists yet" gap `cases/`'s own README section below used to flag as
unconditional. `cases/` (Phase 8) remains the primary harness for the one-shot mode and for every
op `scripts/`'s three scenarios don't happen to touch; both harnesses run against the same
`actiond` binary and are kept side by side rather than merged.

## `scripts/` + `run_tests.py`: the persistent-daemon harness

Three scenarios, each a single NDJSON request line (`scripts/*.json`, pretty-printed on disk for
readability -- `run_tests.py` re-serializes each to one compact line before sending it):

1. **`01_draw_square.json`** -- starting from `fixtures/empty.val` (a genuinely empty pattern, no
   draft blocks, produced by running the daemon itself with no `--pattern` and immediately
   `session.save`-ing), builds a 100x100 unit square (`basePoint` + three `endLine`s + four
   `line`s -- `basePoint` can only anchor *one* point per draft block, so the other three corners
   are formula points relative to it, not more `basePoint` calls), renders it, and saves
   `square.val`/`square.png`.
2. **`02_draw_l_shape.json`** -- same starting fixture, builds an L-shaped outline (a 100x100
   square with a 40x40 notch cut from one corner: 6 points, 6 lines) from scratch, renders it, and
   saves `l_shape_drawn.val`/`l_shape_drawn.png`. Its saved output is also committed as
   `fixtures/l_shape.val`, the seed file scenario 3 imports.
3. **`03_import_l_and_convert_to_rectangle.json`** -- run against a daemon started with
   `--pattern fixtures/l_shape.val` (not `empty.val`; `run_tests.py` handles this per-scenario).
   `pattern.dump`s the loaded geometry, then closes the notch with exactly two `point.edit` calls
   -- `{"op":"point.edit","name":"D","length":"20","angle":"90"}` (point `D`, the notch's one
   truly-interior corner, slides onto the same vertical line `B`-`C`-`E` already sit on, at
   `x=100`, `y=-80` -- **not** onto `C`'s exact position: `VFormula`'s `checkZero` guard rejects a
   formula that evaluates to `0`, matching the interactive dialog's own refusal of a zero-length
   point, so the edit is chosen to land `D` collinear with its neighbors instead of coincident with
   one of them -- either produces the same clean visual edge) and
   `{"op":"point.edit","name":"E","length":"100"}` (point `E` slides out to `(100,-100)`, the
   bounding rectangle's actual corner) -- `pattern.dump`s again to show the result, renders, and
   saves `l_to_rectangle.val`/`l_to_rectangle.png`. **The same 6 point ids and 6 line ids
   throughout** -- nothing is deleted or recreated, unlike `cases/03_import_l_shape_to_rectangle`'s
   own rebuild-based workaround (see that case's own README section below, written before
   `point.edit` existed).

Run all three and check PASS/FAIL:
```sh
python3 run_tests.py
```
Requires `actiond` already built (`build_actiond.bat` at the repo root) at
`../../out/src/app/actiond/bin/actiond(.exe)`; override with the `ACTIOND_EXE` environment
variable. Output lands in `output/` (gitignored) -- open `square.png`/`l_shape_drawn.png`/
`l_to_rectangle.png` by hand to visually confirm the geometry, and in particular confirm
`l_to_rectangle.png`/`l_to_rectangle.val` show a **clean rectangle with no notch and no leftover
duplicate geometry** (unlike `cases/03`'s two-`<piece>` rebuild output, `l_to_rectangle.val` has
exactly the same 6 points and 6 lines `l_shape.val` did, just 2 of them moved).

## The pre-existing `cases/` harness (Phase 8, one-shot mode)

The **primary harness for the one-shot `--actions <file>` mode**; the sub-folders below it
document earlier phases' own harnesses, kept as-is and still runnable.

## Quick start (Phase 8 `cases/` harness)

1. Build `actiond` (see `build_actiond.bat` at the repo root). Every command below assumes
   `out/src/app/actiond/bin/actiond.exe` already exists.
2. Run one case:
   ```sh
   python3 run_batch.py cases/01_square/actions.json /tmp/out01 --save-pattern square.val
   ```
3. Compare the result against its golden reference:
   ```sh
   python3 golden_diff.py /tmp/out01/square.val cases/01_square/expected/square.val
   ```
4. Eyeball `/tmp/out01/square.png` against `cases/01_square/expected/square.png` -- renders are a
   **visual reference for human/AI review**, not byte-diffed (PNG encoding is not guaranteed
   byte-stable across Qt/platform versions the way the XML comparison in `golden_diff.py` is).

Case 3 imports case 2's saved output, so it needs an explicit `--pattern`:
```sh
python3 run_batch.py cases/02_l_shape/actions.json /tmp/out02 --save-pattern l_shape.val
cp /tmp/out02/l_shape.val fixtures/l_shape_seed.val   # already committed; only needed after a real edit to case 02
python3 run_batch.py cases/03_import_l_shape_to_rectangle/actions.json /tmp/out03 \
    --pattern fixtures/l_shape_seed.val --save-pattern rectangle.val
```

Run all three plus their golden diffs in one go:
```sh
for c in 01_square 02_l_shape 03_import_l_shape_to_rectangle; do
    case "$c" in
        03_import_l_shape_to_rectangle) pattern="--pattern fixtures/l_shape_seed.val" ;;
        *) pattern="" ;;
    esac
    out="/tmp/out_$c"
    val=$(ls cases/$c/expected/*.val | xargs -n1 basename)
    python3 run_batch.py cases/$c/actions.json "$out" $pattern --save-pattern "$val" || exit 1
    python3 golden_diff.py "$out/$val" "cases/$c/expected/$val" || exit 1
done
```

Both `run_batch.py` and `golden_diff.py` are plain, dependency-free CLI tools (no `jq`, no
non-stdlib Python packages) with clear exit codes (0 = pass) and stdout/stderr separated for CI
use -- see each file's own module docstring for the full contract.

## What each case proves

- **`cases/01_square`** -- builds a closed 200mm square from a base point, three
  formula-derived points (`endLine`), and four `line` edges; wraps it in a pattern piece
  (`piece.addPatternPiece`); renders and saves it. Proves the baseline "build a simple closed
  piece from scratch" path across the full stack: point tools, `line`, piece assembly, render,
  save -- and gives case 03 a same-side-length bounding box to cross-check against (see below).
- **`cases/02_l_shape`** -- builds an L-shaped piece (6 points, 6 edges: a 200mm square with a
  60mm notch cut from one corner), wraps and renders it, and its saved output is committed as
  `fixtures/l_shape_seed.val` for case 03 to import. Proves multi-segment piece assembly and is
  the fixture-generation step for case 03.
- **`cases/03_import_l_shape_to_rectangle`** -- loads `fixtures/l_shape_seed.val` (does **not**
  build from empty), runs `pattern.dump` first to confirm the loaded geometry matches what case 02
  produced (compare its `responses.json`'s first `pattern.dump` result against
  `cases/02_l_shape`'s own `pattern.dump` output -- same point names/coordinates, same `LShape`
  piece with 6 nodes), then converts the L back into a rectangle and re-renders. Proves "import an
  existing pattern and modify it" end to end, not just "build from scratch" -- see "Case 3's
  edit-vs-rebuild choice" below for exactly what "modify" means here.

### Cross-check: case 01's square vs case 03's rectangle

Both use side length `200` (deliberately kept equal). After running both,
`responses.json`'s `render.snapshot` result's `"boundingBox"` should be **identical** between the
two (verified while authoring this harness: `{"height": 839.4055118110236, "width":
846.6470844648843, "x": -27.741572653860658, "y": -776.4055118110236}` in both -- the extra size
beyond a bare 200x200 comes from the piece's default grainline decoration, present identically in
both). A mismatch here is a real regression, not noise.

### Case 3's edit-vs-rebuild choice

The task this harness was built for offered two options for turning the L back into a rectangle:
edit the notch points' formulas in place, or add new points/lines and rebuild the piece around
them. **No "edit an existing object's formula in place" action exists in this phase's action
layer** (`src/libs/actionlayer/action_registry.cpp` has no `tool.updateFormula` or similar --
this is a documented gap, not an oversight), so case 03 uses the rebuild approach:

1. One new point `G` is added 200mm up from `B` -- the same height as `F`, landing exactly at the
   would-be fourth corner of the full square, entirely bypassing the notch points `C`/`D`/`E`'s
   own 140/60/60 formulas.
2. Two new lines (`B`-`G`, `G`-`F`) connect it into the outline, reusing the L-shape's own
   already-existing `A`-`B` and `F`-`A` lines.
3. A brand-new `Rectangle` piece is built from nodes `[A, B, G, F]`.

`C`, `D`, `E`, the four L-shape-only lines (`B`-`C`, `C`-`D`, `D`-`E`, `E`-`F`), and the original
`LShape` piece are all **left in the pattern file, unused by the new piece** -- there is no
delete/remove action in this phase to clean them up (also a documented gap). Concretely this
means:

- `cases/03_import_l_shape_to_rectangle/expected/rectangle.val` contains **two** `<piece>`
  elements (`LShape` and `Rectangle`), not one.
- `rectangle.png` visually shows **both** the old notch outline and the new rectangle-completing
  edges superimposed -- `render.snapshot` draws every line in the draft scene, not just one
  piece's own boundary, so this is a faithful rendering of real pattern state, not a bug.
- "Exactly 4 outline nodes and 4 lines" (the task's own validation ask) refers to the **`Rectangle`
  piece's own** `<nodes>` list (4 entries) and the 4 line objects that trace its actual boundary
  (`A`-`B`, `B`-`G`, `G`-`F`, `F`-`A`) -- not a claim that the whole file only contains 4 nodes/4
  lines total. `golden_diff.py` compares the whole file structurally (including the leftover
  L-shape geometry, since it's part of the correct/expected output), so this is enforced
  automatically; it is called out here because it is easy to misread the task's own phrasing as
  "the whole file has 4 nodes."

## Known gaps (found and documented while building this harness)

These are genuine, reproduced findings from validating Phase 8's handlers end-to-end via this
harness -- not guesses. See the cited source comments for the full technical detail; this is a
summary for anyone deciding whether to rely on the affected ops.

- **`piece.union` crashes.** Reproduced a hard segfault inside `UnionTool::Create()` ->
  `unitePieces()` (`src/libs/vtools/tools/union_tool.cpp`) against two plain rectangular pieces,
  with both a plausible shared-edge index pair and a deliberately mismatched one (crashes either
  way -- not simply a bad-index input `piece_handlers.cpp` could validate away). Root cause not
  isolated within this investigation's time budget; see the `KNOWN GAP` comment on
  `handlePieceUnion()`'s declaration in `src/libs/actionlayer/handlers/piece_handlers.h`. **Do
  not rely on `piece.union` yet.**
- **`piece.insertNodes` can fail with a caught (non-crashing) exception.** Unlike every other
  mutating op in this action layer, `PatternPieceTool::insertNodes()` pushes a `SavePieceOptions`
  `QUndoCommand` internally; one reproduction surfaced a clean `VExceptionBadId` referencing a
  node id `PrepareNode()` had *just* created moments earlier in the same call. Handled cleanly
  (the batch continues, no crash), but not root-caused. See the `KNOWN GAP` comment on
  `handlePieceInsertNodes()`'s declaration.
- ~~`NameResolver` can resolve a name to a piece-node clone instead of the live original~~ --
  **found, root-caused, and fixed.** Originally surfaced as genuine **run-to-run
  non-determinism** while authoring case 03: the identical `actions.json`, run against the
  identical `fixtures/l_shape_seed.val` build, resolved `endLine "G"`'s `"basePoint": "B"` to a
  *different* object id across separate process runs -- and on at least one run, produced a saved
  `rectangle.val` with a `<calculation>` element referencing an id only ever *defined* later, in
  `<modeling>`, which then **failed to reopen** with `ExceptionBadId: Can't find object Id: , id
  = 14` (the file-corruption half of the bug, not just cosmetic non-determinism). Root cause:
  `piece.addPatternPiece`/`piece.internalPath` clone each named point
  (`VAbstractTool::CreateNode<VPointF>()`) to build a valid piece-path node, and that clone's
  in-memory name is copied verbatim from its source; `VPattern::ParseNodePoint()`
  (`src/app/seamly2d/xml/vpattern.cpp`) does the same when *reloading* a saved piece, so the
  collision reappears on every load, not just within the process that first created it.
  `NameResolver::idForName()` had no preference between a `Draw::Calculation` object and a
  `Draw::Modeling` clone that happen to share a name, so it silently returned whichever one
  `QHash`'s per-process-randomized iteration order produced.

  **The fix:** `NameResolver::idForName()` gained a `Draw`-scoped overload
  (`src/libs/actionlayer/name_resolver.h`/`.cpp`) that filters on `VGObject::getMode()` before
  ever comparing names -- architecturally unable to match a `Draw::Modeling` clone when
  `Draw::Calculation` was requested, regardless of whether that clone exists because it was just
  created in this process or because it was reconstructed while reloading a file. Every
  calculation-context handler in this action layer (`line_handlers.cpp`,
  `formula_point_handlers.cpp`, `curve_handlers.cpp`, `cutpoint_handlers.cpp`,
  `operation_handlers.cpp`, `piece_handlers.cpp`, and `render_handlers.cpp`'s `"highlight"`
  resolution) now uses it; `pattern.resolveName` deliberately still uses the unscoped overload
  (see its own code comment for why). A same-scope name collision now throws a proper
  `Kind::Duplicate` error instead of hitting a release-build-compiled-out `Q_ASSERT_X`, and a
  cross-scope one throws `Kind::WrongScope`, naming which mode the name was actually found in --
  both surfaced in `pattern.dump`/action-result JSON as `"kind"` and (for `WrongScope`)
  `"foundInScope"` fields. **Verified fixed:** case 03 now produces byte-identical
  `rectangle.val` output across repeated runs against the same seed file, and the regenerated
  `rectangle.val` was confirmed to reload cleanly through `actiond`'s own real
  `VPattern::Parse()` path (the same parser Seamly2D's interactive GUI uses) with no error. See
  the `FOUND AND FIXED` comment at the top of `src/libs/actionlayer/handlers/piece_handlers.h`
  for the full writeup, and `src/test/ActionLayerTest/tst_name_resolver.cpp`'s three
  `testScopedLookup*` slots for the regression tests that would have caught this before it ever
  reached a golden file. `golden_diff.py` also gained a standalone structural check
  (`check_calculation_references()`) that independently catches this exact bug class -- a
  `<calculation>` element referencing an id not yet defined by an earlier `<calculation>` element
  -- without needing Seamly2D itself available to try reopening the file.
- **No edit-in-place or delete action exists yet** for any object type (points, lines, pieces,
  ...) -- see "Case 3's edit-vs-rebuild choice" above. **Partially closed 20 Aug, 2026:** `point.edit`
  now supports in-place editing of a `basePoint`'s x/y and a formula-length/angle point's
  `length`/`angle` (see `docs/action-layer-schema.md` and `scripts/
  03_import_l_and_convert_to_rectangle.json` above, which redoes *this exact case* via `point.edit`
  instead of the rebuild below). Editing lines/curves/pieces, and deleting any object type, remain
  open follow-up scope.
- **`lineType` values are not schema-validated at write time.** `line`/formula-point/curve
  handlers pass whatever `"lineType"` string a caller supplies straight through to the saved XML
  with no validation against the schema's actual enumeration (`solidLine`, `dashLine`, `dotLine`,
  `dashDotLine`, `dashDotDotLine`, `none`, `byGroup` -- see
  `src/libs/ifc/schema/pattern/v0.7.4.xsd`). An invalid value (this harness originally used the
  intuitive-sounding but *not actually valid* `"hair"`, copied from an existing Phase 6 fixture)
  writes successfully and only fails the *next* time that file is loaded (`actiond` startup then
  fails with `"Exception: value '<x>' not in enumeration"`, before any action even runs) -- easy to
  miss unless a test round-trips its own output through `--pattern`, which is exactly what case 03
  does and how this was caught. Every action file in `cases/` now omits `lineType` (falling back
  to the handler's own `solidLine` default) for this reason.

## `cases/` layout

```
cases/
    01_square/actions.json          -- see "What each case proves" above
    01_square/expected/square.val   -- golden XML (compare with golden_diff.py)
    01_square/expected/square.png   -- golden render (visual reference only)
    02_l_shape/...                  -- same shape, for l_shape
    03_import_l_shape_to_rectangle/... -- same shape, for rectangle
fixtures/
    base_measurements.smis  -- minimal (zero-entry) measurements file every case loads
    l_shape_seed.val         -- case 02's saved output, imported by case 03
run_batch.py     -- runs one actions.json against actiond; see its own module docstring
golden_diff.py   -- structurally diffs an actual .val against a golden expected/*.val
```

Golden `expected/*.val` files were authored by hand-reviewing the harness's own first successful
run's output (via `golden_diff.py` self-comparison and a plain read of the XML) before being
committed as the regression baseline; re-review and re-copy them (see "Quick start" above)
whenever a case's `actions.json` or an underlying handler intentionally changes its output.

## Other test folders in this directory (earlier phases, unchanged)

- **`scripts/` + `run_action_tests.sh` + `expected/` + `output/`** (this directory's root) --
  Phase 5's own harness for `basePoint`/`line`. Run with `./run_action_tests.sh`.
- **`actionlayer-tests/`** -- Phase 6's harness for the six formula-bearing point tools
  (`endLine`, `alongLine`, `normal`, `bisector`, `shoulderPoint`, `lineIntersect`). Run with
  `cd actionlayer-tests && ./run_tests.sh`.
- **`phase7_measurements/`** -- Phase 7's harness for `measurements.load`/`.recompute`/`.sync`.
  Run with `cd phase7_measurements && ./run_tests.sh`.

All three (like `cases/`) look for `actiond.exe` at `../../out/src/app/actiond/bin/actiond.exe`
by default; override with `ACTIOND_EXE=/path/to/actiond.exe`.

There is also a proper Qt/QtTest C++ unit test suite at `src/test/ActionLayerTest/` (built via its
own `.pro` file, separate from anything in this directory), covering `ActionEngine`,
`NameResolver`, `pattern.dump`, and `render.snapshot` at the C++ level rather than through the
`actiond` CLI. `tst_action_engine_batches.cpp` also covers `ActionEngine::run()`'s error-handling
safety net directly (an unregistered `"op"`, a real op missing a required field, and a handler
that lets a `VException`/`std::exception` escape uncaught) -- see
[`docs/action-layer-schema.md`](../../docs/action-layer-schema.md)'s "Error handling" section for
the full error-`"type"` taxonomy these and the Python harnesses above collectively exercise.
