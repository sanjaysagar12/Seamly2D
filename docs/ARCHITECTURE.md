# ADR: Action Layer as a Standalone Headless Binary

- **Status:** Accepted (decided at Phase 0, holds through Phase 10).
- **Date:** 19 Aug 2026 (decision), last verified against source 20 Aug 2026.

## Decision

The JSON action layer ships as its own standalone, headless binary —
`seamly2d-actiond` (`src/app/actiond/`) — separate from the interactive `seamly2d` GUI
application. It links against Seamly2D's existing core libraries (`vpatterndb`,
`vgeometry`, `vtools`, `ifc`, `vwidgets`, ...) but never against `MainWindow` or any
other interactive-GUI code, and never constructs a visible window.

`ActionContext` (`src/libs/actionlayer/action_context.h`) — the object every action
handler receives — is a plain, header-only bundle of three raw, non-owning pointers:
`VMainGraphicsScene *scene`, `VAbstractPattern *doc`, `VContainer *data` (plus, since
Phase 8, an optional second `pieceScene` for piece-mode tools). It has no constructor
path, default or otherwise, that touches `MainWindow`. `ActionHost::runActions()`
(`src/app/actiond/action_host.h:36`) builds these directly from a loaded pattern file,
"exactly as `MainWindow::LoadPattern()` would" internally, but without ever
instantiating `MainWindow` itself.

## Context / drivers

The intended workflow is an AI-agent-driven autonomous pattern-construction loop: send
one or more JSON actions, render a snapshot image, have the AI verify the result, send
the next action(s). No human interacts with the GUI during this loop. That workflow
needs a process that:

- starts fast and holds pattern state (`VContainer`/`VAbstractPattern`/
  `VMainGraphicsScene`) in memory across many small actions, rather than reloading a
  file per action;
- runs in CI/containers with no display server;
- exposes a narrow, stable JSON contract rather than Seamly2D's interactive widget
  surface.

## Alternatives considered and rejected

**In-process coupling inside `MainWindow`.** Rejected. It would pull in unnecessary GUI
surface area (menus, docks, tool palettes, the whole `MainWindow` widget tree) that a
headless AI loop never touches, and it would need a `CheckFormula` blocking-dialog
workaround, since several existing `VToolXxx::Create(...)` paths pop a modal formula
dialog when running in GUI mode. The standalone daemon avoids this class of problem
entirely: it never sets `isGUIMode() == true`, so those code paths take their headless
branch instead of prompting.

## Consequences

- **No undo/redo GUI integration.** Not needed — there is no human GUI session to undo
  within. Mutating actions apply directly; nothing pushes onto `MainWindow`'s
  `QUndoStack`. (One known exception: `PatternPieceTool::insertNodes()`, reached via
  `piece.insertNodes`, pushes its own internal `SavePieceOptions` `QUndoCommand` — see
  the `KNOWN GAP` comment on `handlePieceInsertNodes()` in
  `src/libs/actionlayer/handlers/piece_handlers.h`.)
- **Two runtime modes, one binary** (`src/app/actiond/main.cpp`):
  - **One-shot** (`--actions <file>`): load a pattern once, run one script, optionally
    `--save-pattern`, print the JSON result, exit.
  - **Persistent NDJSON daemon** (`--actions` omitted, added Phase 9): `--pattern`
    becomes optional (a missing one starts from an empty pattern via
    `PatternSession::createEmpty()`); the process holds a `PatternSession` in memory
    and reads one JSON request per stdin line, dispatching its `"actions"` array and
    writing one JSON response per stdout line, until EOF or a `session.close` action.
    This is the mode the AI-verification loop actually runs against — it is what makes
    "hold pattern state in memory across many small AI-driven steps" concrete, instead
    of paying a full pattern reload per action.
- **Headless by default, not by requirement of the caller.** `main()` sets
  `QT_QPA_PLATFORM=offscreen` itself before constructing `ActiondApplication`, but only
  if the environment doesn't already set it — so `actiond` runs with no display present
  without the person/CI invoking it needing to know that detail, while still allowing an
  explicit override for local debugging against a real platform plugin.
- **Every Seamly2D exception is caught at `main()`'s boundary** (`VException`,
  `std::exception`, and a catch-all) and turned into a single-line
  `{"error": "..."}` on stderr with a non-zero exit — no reused Seamly2D loading call
  (`VPattern::Parse`, `readMeasurements`, ...) is allowed to terminate the process
  uncaught.

## Component diagram

The action layer sits *beside* Seamly2D's existing three-layer pattern model (XML DOM /
`VContainer` / `QGraphicsItem`) — it dispatches into the same `VToolXxx::Create(...)`
factory methods the interactive GUI's tool classes call, and never reimplements
geometry, formula evaluation, or XML serialization itself.

```
                        JSON request (one-shot file, or one NDJSON stdin line)
                                          |
                                          v
                    +--------------------------------------------+
                    |     src/app/actiond (seamly2d-actiond)      |
                    |  main.cpp -> ActionHost / SessionServer      |
                    |  loads/holds VAbstractPattern + VContainer   |
                    |  + VMainGraphicsScene (draft) [+ pieceScene] |
                    +--------------------------------------------+
                                          |
                                          v
                    +--------------------------------------------+
                    | src/libs/actionlayer/action_engine.cpp      |
                    |   ActionEngine::run(ActionContext&, script) |
                    +--------------------------------------------+
                                          |
                                          v
                    +--------------------------------------------+
                    | action_registry.cpp: op name -> handler fn  |
                    |   "basePoint" -> handleBasePoint            |
                    |   "line"      -> handleLine                 |
                    |   "endLine"   -> handleEndLine   ... etc.   |
                    +--------------------------------------------+
                                          |
                                          v
                    +--------------------------------------------+
                    | src/libs/actionlayer/handlers/*.cpp          |
                    |   resolves names via NameResolver,           |
                    |   builds VToolXxx::Create(...) call params    |
                    +--------------------------------------------+
                                          |
                                          v
        (existing Seamly2D core — untouched, reused as-is)
        VToolXxx::Create(...)  -----VPointF/VLineF/... geometry----> VContainer
                |                                                          ^
                +--- writes tool history / <calculation>/<modeling> --> VAbstractPattern (XML DOM)
                |
                +--- adds/updates the tool's -----------------------> QGraphicsItem
                                                                         (VMainGraphicsScene)
                                          |
                                          v
                    +--------------------------------------------+
                    | ActionResult -> JSON response (one line)     |
                    +--------------------------------------------+
```

`NameResolver` (`src/libs/actionlayer/name_resolver.h`/`.cpp`) is the one piece of new
lookup logic the action layer adds on top of `VContainer`: it resolves a caller-supplied
name string to the live object id, scoped by `Draw::Calculation` vs. `Draw::Modeling`
where a handler needs that distinction (added after a real name-collision bug — see
`tests/actionlayer/README.md`'s "Known gaps" section for the full incident writeup).

## Fork-diff hygiene

`git diff <pre-Phase-0 commit>..HEAD -- src/libs/vtools src/app/seamly2d/mainwindow.cpp src/app/seamly2d/mainwindow.h`
is **empty**. Every action-layer file is new and additive:

- `src/libs/actionlayer/` — the engine, registry, context, name resolver, and every
  op handler.
- `src/app/actiond/` — the standalone binary (`main.cpp`, `ActionHost`,
  `PatternSession`, `SessionServer`, `ActiondApplication`).
- `src/test/ActionLayerTest/` — the C++/QtTest unit suite for the above.
- `tests/actionlayer/` — the `run_batch` (C++/qmake, `tests/actionlayer/run_batch/`)
  integration-test harness, driving `actiond` as a subprocess against `scripts/*.json` and
  diffing its JSON response against `expected/*.expected.json` (see its own `README.md`; wired
  into `make check` via `src/test/test.pro`'s `ActionLayerBatchTests` entry).
- `docs/action-layer-schema.md`, this file, and `src/libs/actionlayer/CHANGELOG.md`.

No core Seamly2D file (`src/libs/vtools`, `src/libs/vpatterndb`, `src/libs/vgeometry`,
`src/libs/ifc`, `src/app/seamly2d/mainwindow.*`) has been modified to support the action
layer, and none needed to be: every handler reaches existing behavior purely by calling
already-public `VToolXxx::Create(...)` factory methods and `VContainer`/
`VAbstractPattern` accessors.
