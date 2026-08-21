# Action-layer changelog

This module (`src/libs/actionlayer/` + its standalone host `src/app/actiond/`)
version-bumps independently of Seamly2D's own changelog. Entries are grouped by
implementation phase, matching the phase-numbered commit history on `develop`.

See also [`docs/ARCHITECTURE.md`](../../../docs/ARCHITECTURE.md) for the standalone-binary
decision and [`docs/action-layer-schema.md`](../../../docs/action-layer-schema.md) for the
full op reference.

## Phase 12 — Undo/redo (`session.undo`, `session.redo`, `session.undoStatus`)

- New ops: `session.undo` (`{"count"?}` -> `{"undone","canUndo","canRedo","index","count"}`),
  `session.redo` (mirror), `session.undoStatus` (read-only: `canUndo`/`canRedo`/`index`/`count`
  plus a bounded 5-before/5-after window of labeled steps). Both `session.undo`/`session.redo`
  stop early without erroring once nothing is left to undo/redo — an empty stack is a clean
  `{"undone": 0, ...}` success, not a failure.
- **Grouping:** one JSON action now == one `QUndoStack` macro, regardless of how many
  `VUndoCommand`s its handler pushes internally. `ActionSchema` gained a `mutatesPattern` bool,
  derived automatically from each op's existing `category` (`action_registry.cpp`'s `buildSchema()`
  — `"introspection"`/`"session"` are false, every other category is true), so none of the ~50
  pre-existing `registerAction(...)` call sites needed touching. `ActionEngine::run()` gained two
  optional `std::function` callbacks (`BeginMutatingActionFn`/`EndMutatingActionFn`), invoked
  around a mutating op's dispatch and guaranteed paired by an RAII guard even if a handler throws;
  defaulted to no-ops so every pre-existing direct `ActionEngine::run()` caller (every
  `ActionLayerTest` fixture) keeps compiling and behaving identically. `PatternSession::
  runActions()` is the one place that supplies real `qApp->getUndoStack()->beginMacro()`/
  `endMacro()` lambdas — `ActionEngine` itself still has zero `qApp`/`QUndoStack` dependency,
  preserving the separation `docs/ARCHITECTURE.md`'s ADR already committed to.
- **Found, verified, and correctly worked around a real macro-imbalance quirk:**
  `PatternPieceTool::ToolCreation()`/`InternalPathTool::ToolCreation()` (pre-existing GUI-shared
  code) each call `qApp->getUndoStack()->endMacro()` unconditionally for `Source::FromGui` — the
  mode `piece_handlers.cpp`'s raw-overload calls always use — with no matching `beginMacro()` of
  their own on that path (only the *dialog*-based `Create()` overloads, which this action layer
  never calls, open one). Before this phase that was a harmless `qWarning` no-op; this phase's own
  outer `beginMacro()` now supplies the "matching" begin instead, which actually *fixes* the
  warning as a side effect and still ends up wrapping the whole action's real mutation, since
  nothing is pushed after `ToolCreation()` returns for either op. Documented in detail at
  `pattern_session.cpp`'s own `runActions()` comment.
- **Found and confirmed, by direct code reading, that `point.edit` is the one op in this action
  layer that can push more than one `VUndoCommand` per JSON action** (an `endLine`-created point
  given both `"length"` and `"angle"` in the same call pushes two separate `SaveToolOptions`
  commands). Verified live: both are grouped into one macro, and one `session.undo` call reverts
  both together. No other existing handler was found to open its own nested macro or push more
  than one command per call.
- **CRITICAL, VERIFIED FINDING (the headline result of this phase's investigation) — undoing an
  object's *creation* does not prune the live `VContainer`.** `session.undo` genuinely, correctly
  rewrites the pattern DOM (`AddToCalc::undo()` really does remove the tool's element, and a
  `session.save` issued right after writes a correctly smaller `.val` file) — but there is no
  object-deletion mechanism anywhere in this codebase yet (the same gap Phase 9 already documented
  for explicit deletion, just newly visible here). Every already-alive tool's own
  `FullUpdateFromFile()` override does re-run synchronously on undo/redo with no Qt event loop
  needed (see below), but it only *refreshes* a surviving tool from its DOM element — a tool whose
  element just vanished isn't deleted, `VDrawTool::ReadAttributes()` just logs "Can't find tool
  with id" and returns, leaving the stale object exactly as it was. Reproduced directly via a real
  `actiond` run: after undoing every action in a script, `pattern.dump`/`render.snapshot`/
  `pattern.resolveName` all still report the "undone" objects, completely unchanged; creating a new
  object under the same name then *succeeds* (no collision rejected); the next action that
  references that name by name fails with a structured `{"kind":"duplicate",...}` nameResolution
  error, because both the stale and the fresh object are now live and same-named. **By contrast,
  undoing an in-place *edit* (`point.edit`) is not affected — it correctly restores live geometry
  (x/y), not just the DOM formula string**, because the edited tool's own element is never removed,
  only its attributes are rewritten back and successfully re-read. Full writeup, and the exact
  reproduction, preserved at `src/libs/actionlayer/handlers/session_undo_handlers.h`'s own header
  comment and as a golden-file regression case,
  `tests/actionlayer/scripts/10_undo_redo.json`/`expected/10_undo_redo.expected.json`. Not fixed in
  this phase (would require a real delete/prune step or a full `VPattern::Parse(Document::
  FullParse)` re-run wired into the undo/redo path) — flagged as the primary follow-up.
- **Resolved the open "how does `RedoFullParsing()`'s posted `LiteParseEvent` ever get processed
  with no Qt event loop" question** (`actiond`'s `main()` never calls `QCoreApplication::exec()`,
  in either mode): it doesn't, ever, and that's fine by design, not a latent bug — that branch only
  ever runs on a command's *very first* execution (the original `push()`), at which point the
  in-memory state was already built directly by the tool's own `Create()` factory moments earlier,
  independent of the undo command; every later `redo()` and every `undo()` call instead takes the
  synchronous `emit doc->FullUpdateFromFile()` branch, which runs a signal's connected slots inline
  with no event loop involved at all. Documented at length in `pattern_session.cpp`'s own
  `runActions()` comment, including the correction of an earlier (wrong) assumption during this
  same investigation that this cascade also kept `VContainer` itself in sync (see the finding
  above — it keeps *surviving* tools' geometry in sync; it does not delete anything).
- Registered under category `"session"` (excluded from macro-wrapping, same as `session.save`/
  `session.close` — wrapping the undo/redo mechanism in its own undo macro would be circular).
  `pattern.listTools`'s hand-written mirror list and `docs/action-layer-schema.md` (51 ops total,
  up from 48) both updated in the same change.
- New `tests/actionlayer/scripts/10_undo_redo.json` + `expected/10_undo_redo.expected.json`,
  generated from and verified byte-identical to a real `actiond` run against
  `fixtures/patterns/blank.val` — covers the label window, the multi-command `point.edit` grouping,
  `session.undo` with `"count"` larger than the remaining stack, and the creation-undo staleness
  finding above, all in one script. Not yet run through `run_batch`'s own harness binary in this
  environment: `ActionLayerBatchTests`/`run_batch` (see the "Test harness rebuild" entry below) is
  wired into `src/test/test.pro`'s `SUBDIRS` but was never configured into this checkout's existing
  qmake build tree (predates this phase) — a pre-existing gap, not introduced here.
- **KNOWN GAP, not a regression:** `piece.union`'s raw `Create()` overload pushes no `VUndoCommand`
  at all (`UnionTool` doesn't override `ToolCreation()`, so its base-class version calls
  `AddToFile()` -> `AddToModeling()`, which appends straight to the live DOM with no undo wrapping)
  — if the pre-existing segfault gap (`piece_handlers.h`) is ever fixed, `session.undo` would still
  open/close a macro around it (its category is `"piece"`), but that macro would be empty and
  undoing it would not revert the union. Documented alongside the existing segfault note.
  `measurements.load`/`.recompute`/`.sync` similarly push no `VUndoCommand` (their mutations go
  straight to `VContainer`/`doc->LiteParseTree()`, none of it undo-tracked) — `session.undo`
  crossing a `measurements.sync` boundary consumes that step without reverting the measurement
  values or size/height that were active before it; only the geometry actions immediately
  before/after are actually affected. Documented at `measurements_sync_handlers.h`.
- Verification method: built `actionlayer`/`actiond` for real (MSVC/Qt 6.5.3) and ran the scenarios
  above against a live `actiond.exe`, rather than relying on code reading alone — this is what
  surfaced the `VContainer`-staleness finding above, which a code-reading-only pass would have
  missed (the relevant signal/slot wiring reads as correct; it just doesn't do what an initial
  reading assumes). Building `actiond` in this environment required a separate, pre-existing,
  unrelated fix: `src/libs/vdxf/vdxfpaintdevice.cpp` uses `QPaintDevice::PdmDevicePixelRatioF_
  EncodedA`/`_EncodedB`/`encodeMetricF()`, a Qt 5.6–5.13-only transitional API removed in Qt 6
  (superseded by `PdmDevicePixelRatioScaled`, already handled in the same `switch`) — `vdxf` (a
  Phase 11 build dependency) had never actually been built against this checkout's Qt 6.5.3 before.
  Patched locally only to unblock this phase's own verification, then reverted before finishing
  (`git status` confirms `src/libs/vdxf/` carries no changes from this phase) — flagged here as a
  real, separate, currently-unfixed blocker on building `actiond`/`ActionLayerTest` at all in this
  environment, not something this phase's scope covers fixing.

## Phase 11 — Direct scene export (`export.scene`)

- New op: `export.scene` — writes the current draft scene (Phase A: direct scene export, no
  piece nesting/cutting-layout arrangement) to any of eighteen formats: svg, pdf, ps, eps, the
  five raster formats `render.snapshot` already supports (png/jpg/bmp/ppm/tif), and nine flat
  DXF variants (`dxf-r10` through `dxf-2013`, covering every AutoCAD version R10 through 2013).
- Extracted `render.snapshot`'s padding/aspect-ratio/pixel-size derivation out of
  `render_handlers.cpp` into a new shared helper, `handlers/scene_render_geometry.h`/`.cpp`
  (`computeSceneRenderGeometry()`), so `export.scene` reuses it verbatim instead of re-deriving
  (or subtly diverging from) the same sizing rules; `render.snapshot`'s own behavior, including
  its 4096px raster memory-safety cap, is unchanged.
- **Architecture decision:** `export.scene` calls the same low-level, non-GUI writers
  `MainWindowsNoGUI`'s own export functions do (`QSvgGenerator`, `QPrinter`, `VDxfPaintDevice`)
  directly, rather than linking `actiond` against `MainWindow`/`MainWindowsNoGUI`/
  `ExportLayoutDialog` — preserving both invariants `docs/ARCHITECTURE.md`'s ADR already commits
  to (zero `MainWindow` coupling; an empty fork-diff on `mainwindow.cpp`/`mainwindow.h`). Full
  reasoning, plus the Option A/B trade-off this resolves and why Phase B will need its own pass
  at the same question, is recorded in the new `docs/export-actions-notes.md`.
- New build dependency: `actiond`/`ActionLayerTest` now link `-lvdxf` (previously only
  `seamly2d.pro` did) and declare `QT += svg` (previously deliberately excluded from `actiond.pro`
  — see that file's own updated comment) for `VDxfPaintDevice` and `QSvgGenerator` respectively.
- **Known gap, not a regression:** `ps`/`eps` export shells out to the external `pdftops` tool
  (Poppler/Xpdf), exactly as `MainWindowsNoGUI::exportPS()`/`exportEPS()` already do — on a
  machine without it on `PATH`, those two formats fail cleanly with a specific error rather than
  producing output. Every other format (including every DXF variant) has no external-process
  dependency.
- New `src/test/ActionLayerTest/tst_export_scene.cpp` covering every format family, the
  `.dxf`-with-no-explicit-`format` ambiguity guard, an unsupported-format error, an empty-scene
  guard, and `binaryDXF: true` vs. `false` producing genuinely different output.
- `docs/action-layer-schema.md` gained the `export.scene` entry and an updated coverage summary
  (48 ops total).

## Follow-up: no request-envelope `"version"` field yet

Every action JSON file/request line currently has no top-level `"version"` integer, so
`ActionEngine`/`SessionServer` cannot distinguish an old script's expectations from a
newer schema revision — the schema can only evolve by staying additive. This was
flagged during a docs audit (20 Aug 2026) as a real, still-open gap, not yet
implemented; noted here as a candidate for a future phase rather than added as a
side effect of writing this changelog.

## Test harness rebuild (20 Aug 2026)

- Consolidated five overlapping, independently-grown `tests/actionlayer/` test conventions
  (`actionlayer-tests/`, `cases/`, `phase7_measurements/`, and a flat `scripts/`+`expected/`+
  `fixtures/` set at the root, driven by a mix of `run_batch.py`, `run_tests.py`,
  `run_action_tests.sh`, `golden_diff.py`) into one canonical structure: `fixtures/patterns/`,
  `fixtures/measurements/`, `scripts/`, `expected/`, and a single C++ driver at
  `tests/actionlayer/run_batch/`.
- New `run_batch` (`tests/actionlayer/run_batch/run_batch.pro` + `main.cpp`): a small, Qt-Core-only
  console tool that runs `actiond` as a subprocess per case, diffs its JSON response against a
  golden file, and checks (but never byte-diffs) any rendered PNG. Supports a single-case CLI
  (`run_batch <pattern.val> [<measurements>] <actions.json> [--update]`, arguments classified by
  extension, not position) and a no-argument "run every `scripts/*.json` case" mode. Wired into
  `src/test/test.pro`'s `SUBDIRS` as `ActionLayerBatchTests`, alongside `ParserTest`/
  `ActionLayerTest`/etc., so `make check` runs it (the no-argument mode) automatically.
- Seven new self-contained scripts (`01_dump_only.json` through `07_error_cases.json`) replace
  every prior phase's own ad hoc script set, each running independently against
  `fixtures/patterns/blank.val` + `fixtures/measurements/sample.smis`. `piece.union` is
  deliberately not exercised (still segfaults; see `piece_handlers.h`'s own `KNOWN GAP` comment) --
  including it in a script wired into `make check` would crash the whole suite instead of failing
  one case.
- **Found and fixed a real bug while building this harness:** `pattern.dump`
  (`pattern_dump_handler.cpp`) walked `VContainer::DataGObjects()` (a `QHash`) directly; `QHash`'s
  iteration order is randomized per process (Qt's hash-flooding mitigation), so the identical
  script run twice against the identical fixture produced its `"objects"` array in a different
  order each time -- a real API-determinism issue (an AI caller reading `pattern.dump` twice would
  see the object list reorder itself for no reason), not just a test-harness inconvenience. Fixed
  by sorting the ids before serializing. Verified fixed by running every case in the new suite
  twice in a row and confirming byte-identical output both times.

## Phase 10 — Validation, schema, and test harness

- Hardened `ActionEngine`'s error handling: an unregistered `"op"`, a registered op
  missing a required field, and a handler that lets a `VException`/`std::exception`
  escape uncaught are now all caught and reported as structured per-action errors
  instead of crashing the batch.
- `docs/action-layer-schema.md` written/expanded as the hand-maintained schema
  reference for every op, generated from `action_registry.cpp`'s registration list and
  each handler's own header doc-comment.
- `src/test/ActionLayerTest/` gained `tst_action_engine_batches.cpp`, covering the
  above error paths directly at the C++ level.

## Phase 9 — Daemon session protocol

- Added the persistent NDJSON daemon mode to `seamly2d-actiond`
  (`src/app/actiond/session_server.h`/`.cpp`, `pattern_session.h`/`.cpp`): started
  when `--actions` is omitted, it holds a `PatternSession` in memory and reads one
  JSON request per stdin line, dispatching its `"actions"` array and writing one JSON
  response per stdout line, until EOF or a `session.close` action.
- `--pattern` became optional in daemon mode (a missing one starts from an empty
  pattern via `PatternSession::createEmpty()`); `--output-dir` controls where relative
  `render.snapshot`/`session.save` paths resolve.
- New ops: `point.edit` (in-place edit of a `basePoint`'s x/y or a formula
  length/angle point's `length`/`angle` — the first edit-in-place action of any kind),
  `session.save` (in-script equivalent of `--save-pattern`), `session.close` (ends the
  daemon's read loop after its batch's response is written).

## Phase 8 — Full tool coverage + test harness

- Registered handlers for the remaining major tool families: curves (`spline`,
  `splinePath`, `cubicBezier`, `cubicBezierPath`, `arc`, `arcWithLength`,
  `ellipticalArc`), cut/intersection points (`cutSpline`, `cutArc`,
  `pointOfIntersectionArcs`, `pointOfIntersectionCircles`, `pointOfIntersectionCurves`,
  `curveIntersectAxis`, `pointFromCircleAndTangent`, `pointFromArcAndTangent`,
  `triangle`, `height`), transform/group operations (`move`, `rotation`,
  `mirrorByLine`, `mirrorByAxis`, `group`, `trueDarts`), and pattern-piece assembly
  (`piece.addPatternPiece`, `piece.addAnchorPoint`, `piece.internalPath`,
  `piece.insertNodes`, `piece.union`).
- `ActionContext` gained an optional second `pieceScene` pointer (piece-mode tools add
  their graphics items to a scene distinct from the draft-mode `scene()`), defaulted to
  `nullptr` so every earlier call site keeps compiling unchanged.
- Found and fixed a `NameResolver` name-collision bug: a piece-node clone
  (`VAbstractTool::CreateNode<VPointF>()`) copies its source point's name verbatim, so
  a `Draw::Calculation` object and a `Draw::Modeling` clone could share a name with no
  way to disambiguate, producing run-to-run non-deterministic id resolution and, in one
  reproduction, a saved pattern file with a forward-referencing `<calculation>` element
  that failed to reload. Fixed by giving `NameResolver::idForName()` a `Draw`-scoped
  overload that filters on `VGObject::getMode()` before comparing names; every
  calculation-context handler now uses it. A same-scope collision now throws a
  structured `Kind::Duplicate` error; a cross-scope one throws `Kind::WrongScope`
  naming the mode the name was actually found in.
- Documented, not-yet-fixed gaps found while building this phase's test harness:
  `piece.union` segfaults inside `UnionTool::Create()` (root cause not isolated);
  `piece.insertNodes` can throw a caught `VExceptionBadId` referencing an id it just
  created; no delete or edit-in-place action existed yet for any object type (partially
  closed by Phase 9's `point.edit`); `lineType` values are passed through to saved XML
  with no schema validation at write time.
- New `tests/actionlayer/cases/` + `run_batch.py` + `golden_diff.py` harness (one-shot
  mode), with golden `.val`/`.png` references for three end-to-end cases (build a
  square, build an L-shape, import + convert the L-shape to a rectangle).

## Phase 7 — Measurement actions

- New ops: `measurements.load` (swap the active measurement file without
  recomputing), `measurements.recompute` (re-evaluate every formula and rebuild
  geometry), `measurements.sync` (`load` immediately followed by `recompute`).
- New `tests/actionlayer/phase7_measurements/` harness.

## Phase 6 — Formula-bearing point tools

- New ops: `endLine`, `alongLine`, `normal`, `bisector`, `shoulderPoint`,
  `lineIntersect` — the first handlers whose parameters are formula strings passed
  through to `CheckFormula`/qmuparser unresolved, rather than literal values.
- New `tests/actionlayer/actionlayer-tests/` harness covering all six.

## Phase 5 — First mutating actions

- New ops: `basePoint` (creates a new draft block's anchor point) and `line`
  (connects two named points) — the first actions that mutate pattern state rather
  than only reading it.
- `--save-pattern` added to `actiond`'s one-shot mode so a script's resulting `.val`
  file can be inspected.
- New root `tests/actionlayer/scripts/` + `run_action_tests.sh` harness.

## Phase 4 — Name resolution

- Added `NameResolver` and the `pattern.resolveName` diagnostic op (name -> id/type
  resolution), the lookup layer every later mutating handler builds on.

## Phase 3 — Rendering action

- New op: `render.snapshot` (scene-to-image rendering).

## Phase 2 — Standalone headless action host

- Added `src/app/actiond` (`seamly2d-actiond`): the standalone, headless binary this
  module runs as (see [`docs/ARCHITECTURE.md`](../../../docs/ARCHITECTURE.md) for the
  full decision record). One-shot mode only at this phase (`--pattern` +
  `--measurements` + `--actions <file>` [+ `--save-pattern`]); the daemon mode came
  later, in Phase 9.

## Phase 1 — Read-only introspection

- New ops: `pattern.dump` (walks `VContainer::DataGObjects()` and
  `VAbstractPattern::getHistory()` and serializes to JSON), `pattern.listTools`
  (static op-name capability listing), `pattern.listMeasurements`.

## Phase 0 — Fork setup & module scaffolding

- `src/libs/actionlayer/` created as an additive module: `ActionEngine`,
  `ActionRegistry`, `ActionContext`, `NameResolver` scaffolding.
