# Export actions: architecture notes

This records the architecture decision behind `export.scene` (Phase A, implemented 21 Aug 2026)
and the trade-off it resolves, so a future Phase B (`export.layout`/`export.pieces`, nested
cutting-layout export) starts from an accurate picture instead of re-deriving it.

## The question

Seamly2D's GUI export actions ("Export Layout As", "Export Pieces As", "Export Draft Blocks As")
are implemented on `MainWindowsNoGUI` (`src/app/seamly2d/mainwindowsnogui.h`/`.cpp`), a
`QMainWindow` subclass `MainWindow` derives from, driven through `ExportLayoutDialog`
(`src/app/seamly2d/dialogs/export_layout_dialog.h`, a real `QDialog`). Seamly2D's own `--export`
CLI mode already proves this pipeline runs correctly without ever showing a window — see
`MainWindow::DoExport()` (`mainwindow.cpp:7714`), which drives `ExportLayoutDialog` purely
through setters and never calls `.exec()`/`.show()` on it.

`docs/ARCHITECTURE.md`'s existing ADR (Phase 0, still in force) commits `actiond` to two
invariants: it "never [links] against `MainWindow` or any other interactive-GUI code," and its
"Fork-diff hygiene" section treats `git diff <pre-Phase-0>..HEAD -- ... mainwindow.cpp
mainwindow.h` being empty as a literal correctness check. So: can `export.scene` reuse
`MainWindowsNoGUI`'s already-correct, already-tested export pipeline without breaking either of
those?

## What was actually found

- `MainWindowsNoGUI` is itself still abstract (two of its own pure virtuals —
  `CleanLayout()`/`PrepareSceneList()` — plus four inherited from `VAbstractMainWindow`), and its
  higher-level entry points (`ExportData()`, `LayoutSettings()`, `ExportFlatLayout()`,
  `ExportApparelLayout()`) depend on `ExportLayoutDialog` and, for nested-layout formats, on
  `VLayoutGenerator`'s stateful nesting run. None of that machinery is actually needed for
  Phase A's scope (the current draft scene, as-is, with no piece nesting).
- The functions that *do* matter for Phase A — `MainWindowsNoGUI::exportSVG()`/`exportPDF()`/
  `exportPS()`/`exportEPS()`/`FlatDxfFile()` (`mainwindowsnogui.cpp:899-1198`) — call into plain,
  non-`QMainWindow`-coupled primitives: `QSvgGenerator`, `QPrinter`, `VDxfPaintDevice`
  (`src/libs/vdxf/vdxfpaintdevice.h`, a plain `QPaintDevice`), and (for PS/EPS) an external
  `pdftops` process. None of these five functions is `MainWindowsNoGUI`-specific in any load-
  bearing way — they take a scene/paper rect and a filename and write a file.

This means the task's literal "Option A" (extract `ExportData`/etc. out of `MainWindowsNoGUI`
into a new shared class *both* `MainWindow` and `actiond` link against) and "Option B" (link
`actiond` against `MainWindowsNoGUI` directly) are both more than Phase A actually needs, and
both carry a real cost the narrower option below avoids:

- Option A, done as literally described, means editing `mainwindow.cpp` itself (to call the new
  shared class instead of its own private methods) — directly breaking the fork-diff-hygiene
  invariant.
- Option B means giving `actiond` a real dependency on a `QMainWindow` subclass and a `QDialog`
  with a `.ui` widget tree (`ExportLayoutDialog`), plus inheriting `PdfTiledFile()`'s
  `ContinueIfLayoutStale()` modal-`QMessageBox` hazard on any code path that reaches it without a
  prior successful `LayoutSettings()` call — exactly the class of problem the ADR's "Alternatives
  considered and rejected" section already argued against for `MainWindow` itself.

## What was chosen for Phase A

`export.scene` (`src/libs/actionlayer/handlers/export_handlers.cpp`) calls `QSvgGenerator`,
`QPrinter`, and `VDxfPaintDevice` **directly**, mirroring what `MainWindowsNoGUI`'s five writer
functions do function-for-function, without linking against or subclassing
`MainWindow`/`MainWindowsNoGUI`/`ExportLayoutDialog`, and without modifying `mainwindow.cpp`/
`mainwindowsnogui.cpp` at all. Concretely this is a third option, narrower than either the task's
Option A or Option B:

- **Zero `MainWindow` coupling is preserved** — `actiond` still links no interactive-GUI class,
  matching the ADR exactly as it already reads.
- **Fork-diff hygiene is preserved** — `mainwindow.cpp`/`mainwindow.h` remain untouched;
  `mainwindowsnogui.cpp` (not covered by that specific diff check, but touched by neither Option
  A nor this choice) is also untouched.
- **No new modal-dialog hazard** — nothing in `export.scene`'s code path can pop a `QMessageBox`.
- The cost is a small amount of duplicated *dispatch* logic (which format calls which writer,
  and the `QPainter`/target-rect/source-rect setup each writer needs) — not duplicated
  *implementation*, since the actual writers (`QSvgGenerator`, `QPrinter`, `VDxfPaintDevice`) are
  Qt/Seamly2D classes reused as-is, unmodified, by both `export.scene` and
  `MainWindowsNoGUI`'s own functions.

One deliberate behavioral improvement over the GUI baseline: `MainWindowsNoGUI::
exportDraftBlocksAs()` (the GUI's closest equivalent to `export.scene` — direct draft-scene
export, no nesting) has a real, pre-existing bug where its DXF case is a documented no-op
(`default: break`) and its `PDFTiled`/`OBJ` cases silently fall through to the PS writer.
`export.scene` does not need to replicate that bug — its DXF path actually works.

## Why this will need a fresh look for Phase B

Phase B (`export.layout`/`export.pieces`, nested cutting-layout export) is a fundamentally
different problem: it needs `VLayoutGenerator`'s actual nesting *run* (`Generate()`, synchronous,
stateful, populates `papers`/`pieces`/`piecesOnLayout`/`scenes` as `MainWindowsNoGUI` member
fields) before any per-format writer can run at all, and AAMA DXF export
(`VDxfPaintDevice::ExportToAAMA()`) needs the placed `VLayoutPiece` geometry that same nesting run
produces, not just a scene to render. That is substantially more state and control flow than
Phase A's "call a writer directly" approach can absorb by just avoiding `MainWindowsNoGUI` outright.

Phase B should re-ask the Option A/B/(this) question from scratch once that nesting-state
question is scoped, rather than assuming Phase A's answer (write it fresh, call the low-level
primitives directly) automatically extends to it. A real extraction (this document's version of
"Option A" — a new, plain, non-`QMainWindow` class such as `PatternExporter` that owns the
nesting-result state and that *both* `MainWindow` and `actiond` are refactored to call) becomes
more attractive once there's real, non-trivial stateful logic worth sharing rather than a handful
of independent per-format writer calls — but doing that well means `mainwindow.cpp` changing,
which is a decision this document deliberately leaves to whoever scopes Phase B, not one made
here by default.
