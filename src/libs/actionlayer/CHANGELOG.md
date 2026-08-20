# Action-layer changelog

This module (`src/libs/actionlayer/` + its standalone host `src/app/actiond/`)
version-bumps independently of Seamly2D's own changelog. Entries are grouped by
implementation phase, matching the phase-numbered commit history on `develop`.

See also [`docs/ARCHITECTURE.md`](../../../docs/ARCHITECTURE.md) for the standalone-binary
decision and [`docs/action-layer-schema.md`](../../../docs/action-layer-schema.md) for the
full op reference.

## Follow-up: no request-envelope `"version"` field yet

Every action JSON file/request line currently has no top-level `"version"` integer, so
`ActionEngine`/`SessionServer` cannot distinguish an old script's expectations from a
newer schema revision — the schema can only evolve by staying additive. This was
flagged during a docs audit (20 Aug 2026) as a real, still-open gap, not yet
implemented; noted here as a candidate for a future phase rather than added as a
side effect of writing this changelog.

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
