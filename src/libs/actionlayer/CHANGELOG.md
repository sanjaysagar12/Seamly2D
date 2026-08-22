# Action-layer changelog

This module (`src/libs/actionlayer/` + its standalone host `src/app/actiond/`)
version-bumps independently of Seamly2D's own changelog. Entries are grouped by
implementation phase, matching the phase-numbered commit history on `develop`.

See also [`docs/ARCHITECTURE.md`](../../../docs/ARCHITECTURE.md) for the standalone-binary
decision and [`docs/action-layer-schema.md`](../../../docs/action-layer-schema.md) for the
full op reference.

## Phase 15 — Cross-draft-block dangling reference (bug fix)

- **Root cause, traced to `VPattern::parseDraftBlockElement()`
  (`src/app/seamly2d/xml/vpattern.cpp`):**

  ```cpp
  case 0: // TagCalculation
      data->ClearCalculationGObjects();   // wipes every calculation-scope object from every
                                           // previously-parsed draft block, unconditionally
      ParseDraftStage(domElement, parse, Draw::Calculation);
  ```

  Every `<draftBlock>`'s own `<calculation>` section, when (re)parsed, wipes all calculation-scope
  points/lines/curves loaded from every *other* draft block already parsed so far — confirmed
  directly in `VContainer::ClearCalculationGObjects()`, which unconditionally clears every object
  with `getMode() == Draw::Calculation` regardless of which draft block it came from. **This is
  correct, intentional, load-bearing behavior, not touched by this fix:** draft blocks are
  self-contained calculation scopes, never meant to interlink — the same invariant `basePoint`'s own
  handler comment already documents (`VToolBasePoint::AddToFile()` unconditionally starts a
  brand-new block, with no merge-into-existing-block logic).
- **The bug:** nothing in `piece_handlers.cpp` previously checked that a `"nodes"`/`"point"`
  argument's resolved id was created in the *same* draft block as the pattern's own
  currently-active one (`VAbstractPattern::getActiveDraftBlockName()`). Referencing a point from a
  *different*, earlier draft block succeeded silently in the live session — `VContainer` is only
  ever wiped during file *parsing*, never mid-session — producing a piece/anchor whose modeling
  clone referenced an `idObject` belonging to a different block. **Reproduced directly, not
  assumed:** built `"SquareBlock"` first, then `"RectangleBlock"` (making it the active block),
  then called `piece.addPatternPiece` referencing `"SquareBlock"`'s own points — this used to
  succeed with no error. On reload, `"RectangleBlock"`'s own `<calculation>` section (parsed after
  `"SquareBlock"`'s) wipes `"SquareBlock"`'s points first, so its own `<modeling>` clone's lookup
  throws `VExceptionBadId`, caught and **silently skipped** by `VPattern::ParseNodePoint()`
  (`"Possible case. Parent was deleted, but the node object is still here."` — a legitimate case for
  an actually-deleted parent, but a false positive here) — leaving the piece's later reference to
  that never-created clone to fail with exactly the originally-reported
  `ExceptionBadId: Can't find tool in table., id = N`. Same failure class as the earlier `SetMPath`
  measurement-path bug: works fine live, invisible to any in-process check, corrupts only on reload.
- **Fix:** a new `checkSameDraftBlock()` helper (`piece_handlers.cpp`), backed by a new
  `draftBlockForObjectId()` helper that traces an object id's own draft block via a
  `doc->getHistory()` scan (`VToolRecord::getDraftBlockName()` — the same field
  `VAbstractTool::AddRecord()` already stamps every tool's history entry with, using whatever
  `getActiveDraftBlockName()` was at *that* tool's own creation time — no new tracking mechanism
  invented). Wired into `resolvePointNodes()`/`resolvePreparedPointNodes()` (the two shared
  `"nodes"`-array resolvers behind `piece.addPatternPiece`/`piece.internalPath`/
  `piece.insertNodes`) and inline into `handlePieceAddAnchorPoint()` (a single `"point"`, not a
  `"nodes"` array) — every handler that builds a piece-scoped DOM structure from an
  externally-named point was audited, not just the one that happened to be reproduced. A mismatch
  is rejected with a structured `{"type":"crossDraftBlockReference","message","offendingNodes",
  "expectedDraftBlock","foundDraftBlock"}` error.
- **Atomicity fix, found while implementing the check above:** `resolvePreparedPointNodes()`
  previously resolved a name and immediately cloned it, one name at a time, in a single loop — so a
  validation failure partway through (the polygon-closure check, or now this new cross-draft-block
  check) would have already left every already-processed name's modeling clone dangling in
  `VContainer`, unreferenced by anything, with no cleanup. Restructured into three explicit phases
  (resolve every name → validate the whole set → only then clone each one) so *any* failure —
  including every pre-existing failure mode, not just the new one — now leaves zero dangling clones
  behind. Confirmed directly: the saved `.val` from the negative regression test below contains
  exactly one `<piece>` and exactly the four `<modeling>` clones the one legitimate piece actually
  needed, nothing from any of the three rejected calls.
- **Known, tracked gap left by this fix (not silently discovered later):** an operation-created
  destination point (`move`/`rotation`/`mirrorByLine`/`mirrorByAxis`) has no individual
  `doc->getHistory()` entry of its own — only the *operation* tool's own id does (confirmed by
  reading each of those four tools' own `AddRecord()` call). Such a point's own draft block cannot
  be determined this way, so `checkSameDraftBlock()` treats it as unverifiable rather than a
  violation, instead of either false-blocking a legitimate reference or attempting a more complex
  reverse trace back to the owning operation tool. A cross-draft-block reference through an
  operation's result specifically would not be caught by this fix.
- **`piece.union` audited and confirmed unaffected:** it references only two already-existing
  pieces by name/id, each already fully self-contained with its own previously-validated modeling
  clones — uniting them creates no new clone referencing a calculation-scope point, so there is
  nothing here for a draft-block mismatch to corrupt. (Its own separate, pre-existing, already-
  documented segfault gap is untouched by this fix either way.)
- New `tests/actionlayer/scripts/13_cross_draft_block_error.json` (negative): reproduces the exact
  original scenario against all three affected "nodes"/"point"-taking ops
  (`piece.addPatternPiece`, `piece.internalPath`, `piece.addAnchorPoint`), confirms each now fails
  immediately with the structured error, and confirms — via a trailing `piece.list` showing exactly
  one piece — that none of the three failed calls left a dangling piece, internal path, or
  anchor-point clone behind.
- New `checkSharedDraftBlockMultiPieceRegression()` in `run_batch/main.cpp` (positive, bespoke —
  same "needs a genuinely fresh process, not just a scripts/*.json diff against output from the
  same process that built the pattern" reasoning as `checkMeasurementsPathRegression()`, since this
  specific bug class is invisible to any in-process check by definition): builds two pieces sharing
  construction points within **one** shared draft block (mirroring Aldrich's own real file
  structure, Phase 13's fixture — never a second `basePoint` call), saves, then reloads the saved
  file into a **second, genuinely separate `actiond` process** and confirms both pieces survive
  with a real `piece.list`. **Also verified directly against the real, unmodified `seamly2d.exe`
  GUI binary** (`--test <path>`, exit code 0 = clean load — the exact same `VPattern::Parse()` path
  `MainWindow::LoadPattern()` uses): the saved file from this check opens with no parse error,
  which is the test that actually matters, since an `actiond`-only check would not have caught the
  original bug either (the fixture files needed no rebuild for this — `seamly2d.exe` never links
  the action layer at all, so its baseline binary already reflects the correct, pre-existing, real
  GUI parsing behavior this fix's *saved output* now has to satisfy).
- `12_piece_placement_and_grouping.json` (Phase 14) needed restructuring alongside this fix: its
  four pieces were originally all built *after* all four of their independent draft blocks, so by
  the time each `piece.addPatternPiece` ran, only the *last* block was active — an accidental,
  unnoticed instance of the exact bug this phase fixes, caught by this phase's own new validation
  rejecting that test's own golden run. Fixed by interleaving: build one draft block, immediately
  create that block's own piece, then move to the next block — the legitimate pattern this
  validation requires, and a good illustration of why the fix matters even for scripts that "look"
  unrelated to the reported bug.
- `docs/action-layer-schema.md`'s [Pieces](#pieces) section gained the full constraint writeup,
  the `crossDraftBlockReference` error-type table entry, and a per-op "Known error cases" mention
  on `piece.addPatternPiece`/`piece.internalPath`/`piece.insertNodes`/`piece.addAnchorPoint`, plus
  an explicit "not affected" note on `piece.union`.

## Phase 14 — Piece placement (bug fix) + optional per-piece grouping (new feature)

**These are two separate, independently-found issues. Keep them separate when reading this
entry: only the first was ever actually broken.**

### Bug fix: `piece.addPatternPiece` pieces defaulted to identical position, with no way to avoid it

- **Root cause, confirmed by reading `pattern_piece_tool.cpp:1586`
  (`PatternPieceTool::RefreshGeometry()`):** `this->setPos(piece.GetMx(), piece.GetMy())` applies a
  genuine position offset on top of a piece's own node-derived local shape — but `VPiece`
  default-constructs `mx`/`my` to `(0,0)`, and neither `PatternPieceTool::Create()` nor
  (before this phase) `piece.addPatternPiece`'s own handler ever assigned anything else. In
  interactive use a human drags each new piece to a clear spot in the Piece/Layout view after
  creating it — the real GUI doesn't auto-place pieces either — but a headless/AI-only caller has
  no equivalent step, so every piece built with no explicit position landed at the exact same
  place.
- **Reproduced directly before writing any fix, not assumed:** two pieces (`"Front"`/`"Back"`)
  built from two independently-drafted `basePoint`-anchored draft blocks that both happen to start
  at the same origin — a realistic, common scenario, not a contrived edge case (`basePoint`
  unconditionally starts a brand-new draft block per call, confirmed correct/intentional and left
  untouched by this phase; see `point_handlers.cpp`'s own comment) — rendered completely on top of
  each other; `render.snapshot`'s whole-scene output showed only one square, the other entirely
  hidden underneath it.
- **Fix:** new optional `"mx"`/`"my"` parameters on `piece.addPatternPiece`, passed straight
  through to `VPiece::SetMx()`/`SetMy()`. Giving *either* one (even just one of the two) is always
  taken exactly as given (the other defaulting to `0`) — auto-placement never touches an explicit
  value. Omitting *both* triggers a new session-lifetime `PieceLayoutCursor`
  (`piece_layout_cursor.h`, a small header-only class) that places the piece clear of every piece
  already assembled this session: a simple left-to-right "shelf" layout, wrapping to a new row past
  a width threshold — deliberately not a real cutting-layout/bin-packing algorithm, which is a
  distinct, much larger feature already flagged out of scope as `export.scene`'s own "Phase B" work.
  The response always echoes back the position actually used (`"mx"`/`"my"`), whichever path
  produced it, so a caller can see the resolved position either way without re-deriving it.
- **Wiring:** `ActionContext` gained an optional 5th constructor parameter, `PieceLayoutCursor
  *pieceLayoutCursor` (defaults to `nullptr`, so every existing call site — `ActionHost`'s
  now-delegated-to-`PatternSession` path, every `ActionLayerTest` fixture — keeps compiling and
  behaving unchanged; a context with no cursor falls back to `mx=my=0`, exactly matching this op's
  pre-existing behavior). `PatternSession` owns the one real instance (`m_pieceLayoutCursor`,
  session-lifetime, declared before `m_context` since its address is taken in `m_context`'s own
  initializer — member declaration order, not initializer-list order, decides construction order
  in C++) and passes its address into `m_context`'s constructor — `ActionHost::runActions()` now
  just delegates to `PatternSession::loadFromFile()` (confirmed by reading `action_host.cpp`), so
  this same wiring covers both the one-shot `--actions <file>` CLI mode and the persistent NDJSON
  daemon mode with no separate code path to keep in sync.
- **Found and ruled out a false lead while building the regression test for this:** an early
  version of the new test picked an explicit `"mx"`/`"my"` for a third piece that happened to
  genuinely overlap a second, auto-placed piece — the resulting render looked exactly like a
  rendering bug (a piece's seam-allowance fill "missing," outlines double-exposed) until
  isolating it (removing pieces one at a time) showed it was real, correct overlap from a
  poorly-chosen test coordinate, not a defect in the placement fix or in `PatternPieceTool`
  rendering itself. Recorded here so nobody re-investigates the same dead end: `render.snapshot`
  `target: "piece"` renders whichever piece it's asked to render — including piece geometry from a
  *different*, genuinely overlapping piece, exactly as it should, if that overlap is real.

### New feature (explicit opt-in, not a correction of previously-broken behavior): `createGroup`

- New optional `"createGroup"` (default `false`) and `"groupName"` parameters on
  `piece.addPatternPiece`: when `createGroup` is `true`, also creates a group containing the
  piece's own *original* node points (e.g. `"A"`/`"B"`/`"C"`/`"D"`) — never this piece's own
  internal `"__pieceNode_<id>"`-named clones, which are purely an implementation detail never meant
  to be independently selected — named `groupName` if given, else this piece's own `"name"`.
- **Confirmed this is genuinely optional, not a gap being closed:** `PatternPieceTool::Create()`
  creates no group in the real GUI's own code path either (read directly, not assumed) — Aldrich's
  per-piece groups (see Phase 13's fixture) were built by a human via the Groups panel afterward.
  Worth adding anyway because an AI-only pipeline has no human available for that manual
  organizational step, and an empty Group Manager is a worse experience even though nothing is
  technically broken by it. Defaults to `false` specifically so this never silently changes output
  for any existing caller of `piece.addPatternPiece`.
- **Implementation reuses, rather than reimplements, group creation:** calls `handleGroup()`
  (`operation_handlers.h`/`.cpp`) directly with a synthesized `{"name","sourceObjects"}` — the same
  handler `"group"` itself already dispatches to, which already reuses `AddGroup::redo()`'s
  (`vtools/undocommands/addgroup.cpp`) non-undo-stack logic in turn. The piece has already been
  created successfully by the time this runs, so a `handleGroup()` failure (e.g. a group-name
  collision with an unrelated pre-existing group) is reported via the response's `"groupError"`
  field rather than failing the whole `piece.addPatternPiece` action — the piece really does exist
  either way, and reporting the action as failed would misleadingly suggest otherwise.

### Tests and docs (both issues)

- New `tests/actionlayer/scripts/12_piece_placement_and_grouping.json`: builds `"Front"`/`"Back"`
  from two same-origin draft blocks (no `"mx"`/`"my"`) and asserts, via each
  `piece.addPatternPiece` result's own echoed `"mx"`/`"my"`, that `PieceLayoutCursor` separated
  them (`"Back"`'s `mx` lands exactly `"Front"`'s width + margin to the right); `"Side"` (a third,
  independent block) with explicit, deliberately non-colliding `"mx"`/`"my"` proves those are
  honored exactly, not adjusted; `"Grouped"` (a fourth, independent block) with `"createGroup":
  true` proves the group is created with the expected `sourceObjects`. Two `render.snapshot
  target: "piece"` renders (`"Front"`/`"Back"`) opened and eyeballed, not just checked for
  existence, before committing their golden copies.
- `tests/actionlayer/scripts/06_piece_and_union.json`, `10_pattern_undo.json`, and
  `11_piece_introspection.json` all call `piece.addPatternPiece` with no `"mx"`/`"my"`/
  `"createGroup"` — their golden files gained the new `"mx"`/`"my"` echo fields (each lands at
  `(0, ~377.95)`, the single/first piece placed at this session's origin) but **no** `"group"`/
  `"groupError"` key, serving as the regression guard that `createGroup`'s default (`false`) truly
  leaves the response shape — and the saved `<groups/>` section — unchanged from before this phase.
- `docs/action-layer-schema.md`'s `piece.addPatternPiece` entry gained the four new parameters and
  the extended example request/response.

## Phase 13 — Piece introspection (`piece.list`, `piece.dump`) + `render.snapshot` `target: "piece"`

- **Closes the `render.snapshot` "no piece scene yet" gap** `render_handlers.h`/`.cpp` had
  documented inline since Phase 8: `ActionContext` gained `pieceScene()` in that same phase (for
  `piece_handlers.cpp`'s assembly ops), but `render.snapshot` itself was never updated to use it —
  `target` accepted only `"draft"`, hard-erroring on anything else. New `target: "piece"` value
  (requires a new `"piece"` name-or-id parameter) crops to one piece's own `PatternPieceTool`
  graphics item — `sceneBoundingRect()` plus the existing `"padding"` parameter, not
  `ctx.pieceScene()`'s whole `itemsBoundingRect()`, which would show every assembled piece at once.
  Locates the item via the same `VAbstractPattern::getTool(id)` idiom the `"highlight"` resolution
  already used (a piece's own id doubles as its `PatternPieceTool`'s registered tool id — see
  `PatternPieceTool::Create()`, `pattern_piece_tool.cpp:152`/`181`).
- **Found and fixed a real, if narrow, correctness bug while wiring this up, not merely a
  hypothetical:** `"highlight"` names are resolved via the `Draw::Calculation` scope, so a
  resolved name's live graphics item is only ever added to the *draft* scene — never
  `ctx.pieceScene()`. Before this phase, a `target: "piece"` render with a `"highlight"` entry
  would have computed that (draft-scene) item's `sceneBoundingRect()` and silently transformed it
  into the *piece* render's pixel space anyway — the exact "wrong scene, wrong place" class of bug
  `render_handlers.cpp`'s own pre-existing `Draw::Modeling` comment already warned about for a
  different case. Fixed by checking `item->scene() == scene` (whichever scene is actually being
  rendered) before accepting a highlight; for `target: "piece"` this correctly, cleanly reports
  every `"highlight"` name as skipped rather than drawing it in the wrong place. `showPointNames`
  needed no equivalent fix — verified it is already scene-agnostic (a global `qApp->Settings()`
  flag every point's own paint code reads).
- Extracted `computeSceneRenderGeometry()`'s padding/aspect-ratio/raster-cap math (previously only
  callable against a whole `VMainGraphicsScene`) into a new `computeRenderGeometryForRect()`
  (`scene_render_geometry.h`/`.cpp`), taking an already-known scene-space rect instead — the
  original function is now a thin wrapper over it. Lets `target: "piece"` share the exact same
  sizing rules `target: "draft"`/`export.scene` already use, instead of a third
  independently-maintained copy.
- **New ops: `piece.list`** (no args → `{"pieces": [{"id","name","nodeCount","seamAllowance"}]}`)
  **and `piece.dump`** (`{"piece": "<name or id>"}` → the piece's main-path nodes, internal paths,
  and anchors, each node reporting `{"id","type","reverse","name"?,"x"?,"y"?,"unsupported"?}`).
  Both read-only, registered under category `"introspection"`. Pieces are not `VGObject`s
  (`VContainer::DataPieces()` is a separate hash from `DataGObjects()`), so `pattern.dump` never
  lists them — an AI caller previously had no way to discover a piece's name/id without already
  knowing it, or to inspect a piece's own assembled structure at all short of re-deriving it from
  `pattern.dump`'s flat object list by hand.
- `toolToString()` (`Tool` enum → its C++ name) moved from `pattern_dump_handler.cpp`-local to
  declared in `pattern_dump_handler.h` (alongside the pre-existing `goTypeToString()`), so
  `piece_dump_handler.cpp` reuses the exact same per-node-type stringification `pattern.dump`'s own
  history entries already use, instead of a second, potentially-drifting copy.
- **Confirmed, not assumed, that the arc/curve-node gap `piece_handlers.h` documents for
  `piece.addPatternPiece`/`piece.internalPath`'s `"nodes"` *creation* does not extend to
  `piece.dump`'s *reading* of an existing piece.** Found while working with a real,
  interactively-authored multi-piece master pattern (an Aldrich block-style basic-blocks file —
  seven pieces, some with genuine `NodeArc`/`NodeSpline` main-path nodes alongside `NodePoint`
  ones) rather than the mostly single-piece, from-scratch fixtures this action layer had been
  tested against so far: `VContainer::GetGObject()` resolves an arc/spline piece-node clone's
  identity (id/name) without error even though this action layer cannot construct one itself —
  `piece.dump` reports such a node with `"unsupported": true` (never crashing or silently dropping
  it), while its ordinary `NodePoint` siblings in the same list still resolve full coordinates
  normally. Verified directly against a real `actiond` run before writing the regression check
  below, not inferred from reading the parser code alone.
- New fixtures: `tests/actionlayer/fixtures/patterns/Aldrich-Womens-6th-Ed-Basic-Blocks.sm2d` +
  `tests/actionlayer/fixtures/measurements/Aldrich-Womens-MultiSize-06-14.smms`, a real
  GUI-authored multi-piece pattern (see the finding above) — **provenance/licensing was not
  independently re-verified by this change**; the files were already present, uncommitted, in
  `examples/actionlayer/piece_groups/` when this phase began (Aldrich is a long-published, widely
  reproduced drafting-book block set), and are copied here on that basis; flagged for a human
  reviewer to confirm before merge, not asserted as clear. `run_batch`'s single-case-mode argument
  classifier now also recognizes `.sm2d` as a pattern-file extension (alongside `.val`) — the same
  XML schema `VPattern::Parse()` already reads regardless of extension, so this is not a new
  format, just letting the CLI accept a real `.sm2d` path the way it already accepts `.val`.
- New `tests/actionlayer/scripts/11_piece_introspection.json` — builds two non-overlapping closed
  squares as separate pieces from scratch (against the default `blank.val` fixture, keeping it
  independent of every other case), then exercises `piece.list`, `piece.dump` (round-trip: the
  dumped main-path node coordinates match the points the script itself created earlier in the same
  script), and `render.snapshot target: "piece"` against each piece in turn. The two rendered PNGs
  were opened and eyeballed (not just checked for existence) before committing their golden
  copies: each crop shows exactly one square, tight to its own bounds, with no bleed from the
  other piece 300 units away.
- `render.snapshot target: "piece"` against a nonexistent piece name added to
  `scripts/07_error_cases.json` — confirms a clean, structured `unknownPiece` error (mirroring
  `piece.dump`'s own), not a crash or a silent fallback to the draft scene.
- New bespoke check in `run_batch/main.cpp`, `checkPieceDumpRealFile()` (same pattern as the
  pre-existing `checkListToolsAi()`/`checkMeasurementsPathRegression()`, needed because the
  no-argument suite loop always pairs every `scripts/*.json` case against the *default* fixtures,
  with no per-case override): runs `piece.list`/`piece.dump` against the real Aldrich fixtures
  above and asserts the 7-piece count and the unsupported-curve-node finding described above,
  giving that finding permanent regression coverage rather than a one-off manual confirmation.
- `docs/action-layer-schema.md` gained the `piece.list`/`piece.dump` entries, the extended
  `render.snapshot` `target`/new `piece` parameter documentation, and an updated coverage summary
  (51 ops total, up from 49).

## Phase 12 — Undo (`pattern.undo`)

- **A first design was built, verified working, and then deliberately abandoned in favor of the
  one actually shipped here — recorded so nobody re-proposes it later without re-deriving the same
  problem.** The first design pushed every mutating JSON action's `VUndoCommand`s onto
  `qApp->getUndoStack()` (one `QUndoStack` macro per action) and added `session.undo`/
  `session.redo`/`session.undoStatus`. It worked correctly *within one running `actiond` process*
  (verified directly: multi-command grouping, `count`-based stepping, empty-stack handling, a
  three-process build-save-reload-undo round trip all passed) — but a `QUndoCommand` holds live
  pointers into that one process's in-memory `VPattern`/`VContainer`/scene, so the stack cannot
  survive `session.save` → process exit → a new process reloading the file. That is the exact
  scenario an AI caller working across separate `actiond` invocations against the same saved
  pattern needs to work, so the design was rejected. (Separately, that first design also
  discovered — and would still be worth knowing regardless of which design ships — that undoing an
  object's *creation* did not prune the live `VContainer` even within one process, only the DOM;
  undoing an in-place *edit* did. That finding doesn't apply to this phase's actual design, which
  reverses history at the DOM/history level directly and reparses from the DOM afterward, but is
  recorded here in case anything resurrects the `QUndoStack` approach later.)
- **New op: `pattern.undo`** (`{"count"?}` → `{"undone","remaining","entries":[{"id","kind"},...]}`).
  One-directional — there is no `pattern.redo` — and DOM/history-based rather than
  `QUndoStack`-based, so its effect is exactly what a subsequent `session.save` writes, with no
  live-vs-saved distinction to get wrong. Reverses `doc->getHistory()`'s tail, most-recently-created
  entry first, never out of order: Seamly2D's pattern format only lets a formula reference an
  object that already existed when the referencing tool was created, so strict reverse-chronological
  order can never hit an object something else still depends on — this is what makes the op safe
  with no reference-count/dependency check of its own.
- **New handler file** `handlers/history_undo_handlers.h`/`.cpp`. Classifies each history entry by
  its `Tool` type and constructs the one delete-command class that actually reverses it — verified
  by reading every real `deleteTool()`/`Remove()` override that pushes one: `Tool::BasePoint` →
  `DeleteDraftBlock` (a `basePoint` action creates a whole `<draftBlock>`, not just one point;
  `DelTool` alone would leave an orphaned empty block), `Tool::Piece` → `DeletePiece` (needs the
  piece's own current `VPiece` value, not just its id), everything else (points, lines, curves,
  cut-points, operations, and modeling-scope entries like `InternalPath`/`AnchorPoint`/`NodePoint`)
  → the base `DelTool`. Each command is constructed and its `redo()` called directly — never pushed
  onto any `QUndoStack` (there is nothing that would ever call `undo()` on it).
- **Found, and had to work around, a real architectural boundary:** `DelTool`/`DeletePiece`/
  `DeleteDraftBlock`'s own `redo()` only removes the DOM element and emits `NeedFullParsing()`/
  `FullUpdateFromFile()` — confirmed by reading `VPattern::PrepareForParse()` that neither signal,
  nor `VAbstractPattern::LiteParseTree(Document::LiteParse)` (the one reparse entry point reachable
  from inside an actionlayer handler, which only ever sees a `VAbstractPattern*`), actually prunes
  `VContainer`/`doc->getHistory()`/the scenes — only a genuine `VPattern::Parse(Document::
  FullParse)` does that, and `LiteParseTree()` explicitly refuses `Document::FullParse` ("Lite
  parsing doesn't support full parsing"). `VPattern::Parse()` itself is declared on `VPattern`, not
  `VAbstractPattern` (the same actionlayer/actiond boundary `measurements_sync_handlers.cpp`'s own
  comment already documents for a different call), so only `PatternSession` — which owns the real
  `VPattern` instance — can reach it. `PatternSession::runActions()` is therefore the one place that
  notices a successful `pattern.undo` and re-parses afterward.
- **Found and fixed a real same-batch staleness bug while verifying this, by actually running it,
  not just reading the code:** the first version of that `PatternSession::runActions()` step
  scanned the *whole finished* results array once, after the entire script's dispatch loop had
  already returned, and reparsed only then. A same-script test (`pattern.undo` immediately followed
  by `pattern.dump` in the *same* `"actions"` array) caught this immediately: the later `pattern.dump`
  still saw the pre-reparse, stale state, because the reparse hadn't happened yet at the point it
  ran. Fixed by giving `ActionEngine::run()` a new optional per-action hook,
  `AfterActionFn` (`action_engine.h`/`.cpp`) — invoked immediately after each action's result is
  recorded, before the loop advances to the next action — defaulted to a no-op so every
  pre-existing direct `ActionEngine::run()` caller (every `ActionLayerTest` fixture) keeps
  compiling and behaving identically. `PatternSession::runActions()` is the only real caller that
  supplies a non-empty hook, keeping `ActionEngine` itself free of any `VPattern`/qApp dependency —
  the same separation `docs/ARCHITECTURE.md`'s ADR already holds for the rest of this module.
- **Verified end-to-end, not assumed:** built `actionlayer`/`actiond` for real (MSVC/Qt 6.5.3) and
  ran, via direct `actiond` subprocess invocations: same-batch undo+dump correctness (after the fix
  above); a `Tool::Piece` deletion (`DeletePiece` path), confirming the piece's modeling-scope node
  clones revert their in-memory-only `__pieceNode_N` rename back to their real name once the
  piece's DOM element is gone (piece_handlers.cpp's own comment already predicted this — that
  rename is never written to the saved XML); a `Tool::BasePoint` deletion (`DeleteDraftBlock` path)
  tearing down a whole draft block with no crash and no orphaned entries; `count` exceeding
  available history (clean partial result); `count: 0` and an already-exhausted history (both a
  clean `"undone": 0`, never an error); the `group`/`piece.insertNodes` history-gap (documented
  below) confirmed to skip cleanly onto an older entry, not crash or silently no-op. **The actual
  point of this design — a three-*process* round trip** (process 1: build `A`+`B`, `session.save`,
  exit; process 2: reload that file, build a `line`+another point, `session.save`, exit; process 3,
  which never ran in the same process as 1 or 2: reload process 2's file fresh, `pattern.undo` back
  past process 1's own save boundary) — was also run directly: process 3's post-undo
  `pattern.dump` and re-saved `.val` file were confirmed **byte-identical** to process 1's own
  saved file.
- **KNOWN GAP (documented, not silently under-covered):** `group` (`operation_handlers.cpp`'s
  `handleGroup()`) and `piece.insertNodes` (`piece_handlers.cpp`) both mutate the DOM directly
  without ever calling `VAbstractTool::AddRecord()`, so neither has any entry in
  `doc->getHistory()` at all — confirmed by reading both handlers and by a direct reproduction
  (`pattern.undo` after a `group` action skips straight past it onto the previous, older entry).
  This is a pre-existing gap in what `doc->getHistory()` itself tracks, not something this handler
  introduces or could special-case its way around.
- New `tests/actionlayer/scripts/10_pattern_undo.json` + `expected/10_pattern_undo.expected.json`,
  generated from and verified byte-identical to a real `actiond` run against
  `fixtures/patterns/blank.val` — covers the `Tool::Piece`/`Tool::BasePoint` delete-command paths,
  the same-batch reparse-timing fix, and the exhausted/zero-count contract, all in one script (the
  cross-process scenario above isn't expressible in the single-script `run_batch` format, so it was
  verified separately as described above rather than added as a golden-file case). Not yet run
  through `run_batch`'s own harness binary in this environment — `ActionLayerBatchTests`/
  `run_batch` (see the "Test harness rebuild" entry below) is wired into `src/test/test.pro`'s
  `SUBDIRS` but was never configured into this checkout's existing qmake build tree, a pre-existing
  gap predating this phase, not introduced here.
- Registered under category `"session"` in `action_registry.cpp`; `pattern.listTools`'s
  hand-written mirror list and `docs/action-layer-schema.md` (49 ops total, up from 48) both
  updated in the same change.
- Building `actiond` in this environment required a separate, pre-existing, unrelated fix:
  `src/libs/vdxf/vdxfpaintdevice.cpp` uses `QPaintDevice::PdmDevicePixelRatioF_EncodedA`/
  `_EncodedB`/`encodeMetricF()`, a Qt 5.6–5.13-only transitional API removed in Qt 6 (`vdxf`, a
  Phase 11 build dependency, had apparently never been built against this checkout's Qt 6.5.3
  before). Patched locally only to unblock verification, then reverted before finishing (`git
  status` on `src/libs/vdxf/` shows no changes from this phase) — not fixed here, flagged as a real,
  separate, currently-unresolved blocker on building `actiond`/`ActionLayerTest` at all in this
  environment.

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
