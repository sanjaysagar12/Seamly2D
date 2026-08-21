//---------------------------------------------------------------------------------------------------------------------
//  @file   pattern_session.cpp
//  @author Seamly2D Contributors
//  @date   20 Aug, 2026
//
//  @copyright
//  Copyright (C) 2017 - 2026 Seamly, LLC
//  https://github.com/fashionfreedom/seamly2d
//
//  @brief
//  Seamly2D is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  Seamly2D is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with Seamly2D. If not, see <http://www.gnu.org/licenses/>.
//---------------------------------------------------------------------------------------------------------------------

#include "pattern_session.h" // Brings in the PatternSession declaration this file implements.

#include "../seamly2d/xml/vpattern.h" // Brings in VPattern, the same VAbstractPattern subclass seamly2d itself uses to parse/create .val files.

#include "../../libs/ifc/exception/vexception.h"     // Brings in VException, thrown for every load-time failure.
#include "../../libs/ifc/xml/vabstractpattern.h"     // Brings in VAbstractPattern::SetMPath(), called below to keep a loaded measurement file's path in the saved pattern.
#include "../../libs/ifc/xml/vpatternconverter.h"    // Brings in VPatternConverter, which upgrades older-format pattern files.
#include "../../libs/vformat/measurements.h"         // Brings in MeasurementDoc, used to load and parse the measurement file.
#include "../../libs/vpatterndb/vcontainer.h"        // Brings in VContainer, the data container the pattern and measurements populate.
#include "../../libs/vwidgets/vmaingraphicsscene.h"  // Brings in VMainGraphicsScene, required (non-null) by VPattern::Parse()/CreateEmptyFile() callers.
#include "../../libs/vwidgets/vmaingraphicsview.h"   // Brings in VMainGraphicsView; see the qApp->setSceneView() comment below for why one is needed.
#include "../../libs/vmisc/def.h"                    // Brings in RelativeMPath()/AbsoluteMPath(), used the same way MainWindow::LoadPattern()/SavePattern() do.
#include "../../libs/vmisc/vabstractapplication.h"   // Brings in the qApp accessors used to mirror MainWindow::LoadPattern()'s setup.

#include <QFileInfo>      // Provides QFileInfo::exists(), used for the fast, clear existence check below.
#include <QGraphicsScene> // Provides QGraphicsScene, the type m_currentScene aliases (see the header's own comment on why this is a member, not a local).
#include <QUndoStack>     // Provides QUndoStack, whose beginMacro()/endMacro() runActions() below wires into ActionEngine::run()'s macro-grouping callbacks (Phase 12).

namespace
{
    // Throws a VException with a clear message if the given file does not exist. Checking this up
    // front means a missing file is reported as "file not found", not as a confusing deep XML
    // parser error from inside VPatternConverter/MeasurementDoc. Identical in spirit to the helper
    // action_host.cpp used before this refactor.
    void requireFileExists(const QString &filePath, const QString &kind)
    {
        if (!QFileInfo::exists(filePath))
        {
            throw VException(QStringLiteral("%1 file not found: %2").arg(kind, filePath));
        }
    }

    // Loads measurementsFilePath into data, exactly mirroring MainWindow::LoadPattern()'s own
    // measurement-loading setup (see action_host.cpp's pre-refactor comments for the full
    // rationale of each call).
    //
    // Also records the path back onto doc's own <measurements> element (VAbstractPattern::SetMPath()),
    // exactly as MainWindow::LoadIndividual()/LoadMultisize() do right after their own successful
    // load (mainwindow.cpp) -- without this, a reopened .val file has no idea which measurement
    // file it came from, so every measurement-referencing formula fails to resolve on reopen even
    // though the in-process VContainer this call just populated looks entirely correct.
    void loadMeasurementsFromPath(VContainer *data, VAbstractPattern *doc, const QString &measurementsFilePath)
    {
        requireFileExists(measurementsFilePath, QStringLiteral("Measurements"));

        MeasurementDoc measurements(data);
        measurements.setSize(VContainer::rsize());
        measurements.setHeight(VContainer::rheight());
        measurements.setXMLContent(measurementsFilePath);
        qApp->setPatternType(measurements.Type());
        measurements.readMeasurements();

        doc->SetMPath(RelativeMPath(qApp->getFilePath(), measurementsFilePath));
    }
}

PatternSession::PatternSession(const QString &patternFilePath, const QString &measurementsFilePath)
    // VContainer/scenes/views are heap-allocated (not the stack objects action_host.cpp used)
    // because a PatternSession -- unlike that one-shot function -- has to keep this state alive
    // across many action batches, not just for the duration of one call.
    : m_data(new VContainer(qApp->translateVariables(), qApp->patternUnitP()))
    , m_draftScene(new VMainGraphicsScene())
    , m_pieceScene(new VMainGraphicsScene())
    , m_sceneView(new VMainGraphicsView())
    , m_pieceSceneView(new VMainGraphicsView())
    , m_currentScene(m_draftScene)
    , m_doc(new VPattern(m_data, m_draftScene, m_pieceScene))
    , m_registry()  // Auto-registers every built-in action handler.
    , m_engine(m_registry)
    , m_context(m_draftScene, m_doc.data(), m_data, m_pieceScene)
{
    // Several reused undo-command redo() paths (e.g. AddToCalc::redo(), which every mutating
    // draw-tool's AddToFile() goes through) unconditionally call VMainGraphicsView::NewSceneRect
    // (qApp->getCurrentScene(), qApp->getSceneView()) -- not gated behind any "is there a GUI"
    // check, because in the real app there always is one. Both accessors return nullptr until
    // something calls the matching setter, and NewSceneRect() dereferences the view
    // unconditionally (its SCASSERT(view != nullptr) is a release-build no-op), so the first
    // mutating action's redo() would null-deref and crash the process without this wiring.
    // setCurrentScene() takes QGraphicsScene**, so it must point at a member (m_currentScene),
    // not a local -- its address has to stay valid for this whole session's lifetime, not just
    // through this constructor.
    qApp->setCurrentScene(&m_currentScene);
    qApp->setSceneView(m_sceneView);

    // Attaches each view to its scene so scene->views() is non-empty. Several paint() overrides
    // elsewhere (e.g. VGraphicsSimpleTextItem::paint(), used by every point's name label)
    // unconditionally call scene->views().at(0) -- QList::at() with an out-of-range index is
    // undefined behavior in a release build (no bounds check), so a scene with zero attached views
    // crashes the whole process the first time render.snapshot's scene->render() paints a point
    // with its name label shown.
    m_sceneView->setScene(m_draftScene);
    m_pieceSceneView->setScene(m_pieceScene);

    qApp->setCurrentDocument(m_doc.data()); // Mirrors MainWindow's own setup; some formula/tool code reaches for qApp's "current" document.
    qApp->setCurrentData(m_data);           // Mirrors MainWindow's own setup; some formula/tool code reaches for qApp's "current" data container.

    // RelativeMPath()/AbsoluteMPath() (vmisc/def.cpp) only make sense given absolute inputs -- a
    // relative absoluteMPath is, by their own contract, returned unchanged instead of resolved
    // against the current directory (see RelativeMPath()'s own early-return), because the GUI's own
    // callers only ever pass paths a file-open dialog already made absolute. actiond's --pattern/
    // --measurements are ordinary CLI arguments a caller may reasonably give as relative (see this
    // example in examples/actionlayer/shirt_front_piece/README.md), so both are absolutized here,
    // once, before anything below reads them -- matching what run_batch.exe (tests/actionlayer/)
    // already has to do for the same reason when it invokes actiond as a subprocess.
    const QString absolutePatternFilePath = patternFilePath.isEmpty() ? QString() : QFileInfo(patternFilePath).absoluteFilePath();
    const QString absoluteMeasurementsFilePath = measurementsFilePath.isEmpty() ? QString() : QFileInfo(measurementsFilePath).absoluteFilePath();

    if (absolutePatternFilePath.isEmpty())
    {
        // No pattern file given: build the minimal "new pattern" DOM directly, matching what
        // MainWindow::New()'s dialog-driven path ultimately relies on -- no draft blocks yet; the
        // first "basePoint" action's own doc->appendDraftBlock() call creates the first one.
        // qApp->getFilePath() is left empty here (its default, unset value) -- there is no pattern
        // save path yet for a measurement path to be relative to, so loadMeasurementsIfGiven()'s
        // own RelativeMPath() call falls back to storing an absolute path, exactly as it does for
        // any other empty patternPath; save() re-relativizes it the first time this pattern is
        // actually saved somewhere, same as MainWindow::SavePattern() does for a never-saved file.
        m_doc->CreateEmptyFile();
        if (!absoluteMeasurementsFilePath.isEmpty())
        {
            loadMeasurementsFromPath(m_data, m_doc.data(), absoluteMeasurementsFilePath);
        }
    }
    else
    {
        requireFileExists(absolutePatternFilePath, QStringLiteral("Pattern"));

        // VPatternConverter upgrades an older-format .val file to the schema version
        // VPattern::Parse() expects, exactly as MainWindow::LoadPattern() does.
        VPatternConverter converter(absolutePatternFilePath);
        m_doc->setXMLContent(converter.Convert());     // Loads the (possibly just-upgraded) pattern XML into doc's DOM tree.
        qApp->setPatternUnit(m_doc->measurementUnits()); // Sync qApp's unit to the pattern file's own declared unit, as MainWindow::LoadPattern() does.

        // MainWindow::LoadPattern() only reaches its own equivalent call (setCurrentFile(), which
        // calls qApp->setFilePath()) once the whole load has succeeded, several steps below this
        // point in its own code -- but the MPath resolution right below needs qApp->getFilePath()
        // to already be this pattern's own path *now*, so its RelativeMPath()/AbsoluteMPath() calls
        // resolve against the right file. Nothing before this line reads qApp->getFilePath(), so
        // setting it here instead of at the very end is equivalent for every already-successful-load
        // side effect and additionally makes this one work.
        qApp->setFilePath(absolutePatternFilePath);

        // An explicit measurementsFilePath (the CLI's --measurements flag) overrides whatever
        // measurement file the pattern's own <measurements> element already names, exactly as
        // MainWindow::LoadPattern()'s own customMeasureFile parameter does (mainwindow.cpp):
        // SetMPath() here updates the DOM in place, so the AbsoluteMPath() resolution right below
        // picks up the override the same way it would pick up the pattern's own stored path.
        if (!absoluteMeasurementsFilePath.isEmpty())
        {
            requireFileExists(absoluteMeasurementsFilePath, QStringLiteral("Measurements"));
            m_doc->SetMPath(RelativeMPath(absolutePatternFilePath, absoluteMeasurementsFilePath));
        }

        // Measurements must be loaded before Parse() below: pattern formulas can reference
        // measurement variables, which have to already exist in data by the time they're evaluated.
        // Resolves against doc's own <measurements> element -- either just overridden above, or
        // whatever the pattern file itself already stored -- exactly as MainWindow::LoadPattern()'s
        // own `AbsoluteMPath(fileName, doc->MPath())` + loadMeasurements() call does. This is what
        // lets a pattern saved with a populated <measurements> path (see save()) be reopened with no
        // --measurements flag at all and still resolve its measurement-referencing formulas -- the
        // exact scenario the real Seamly2D GUI already handles this way.
        const QString effectiveMeasurementsPath = AbsoluteMPath(absolutePatternFilePath, m_doc->MPath());
        if (!effectiveMeasurementsPath.isEmpty())
        {
            loadMeasurementsFromPath(m_data, m_doc.data(), effectiveMeasurementsPath);
        }

        // The real, full parse: walks the pattern XML and builds every geometry object and the
        // tool history into data/doc, via the same vtools Create() factories the GUI editor uses.
        m_doc->Parse(Document::FullParse);
    }
}

PatternSession *PatternSession::loadFromFile(const QString &patternFilePath, const QString &measurementsFilePath)
{
    return new PatternSession(patternFilePath, measurementsFilePath);
}

PatternSession *PatternSession::createEmpty(const QString &measurementsFilePath)
{
    return new PatternSession(QString(), measurementsFilePath);
}

PatternSession::~PatternSession()
{
    // m_doc holds pointers into m_data/m_draftScene/m_pieceScene and must be destroyed before any
    // of them are freed below -- QScopedPointer's own automatic destruction happens only *after*
    // this body finishes, which would be too late, so it is reset explicitly here first.
    m_doc.reset();

    delete m_pieceSceneView;
    delete m_sceneView;
    delete m_pieceScene;
    delete m_draftScene;
    delete m_data;
}

// Phase 12 (undo/redo): supplies ActionEngine::run() with the two callbacks that group every
// VUndoCommand a single JSON action's handler pushes into one QUndoStack macro -- see
// action_engine.h's own comment on BeginMutatingActionFn/EndMutatingActionFn for why this wiring
// lives here (in PatternSession) rather than inside ActionEngine itself: it is the one place in
// this call chain allowed to depend on qApp/QUndoStack, keeping the action-layer library itself
// free of that dependency, matching the same separation every earlier phase has preserved (see
// docs/ARCHITECTURE.md's ADR). abortOnFirstError is forwarded unchanged; only the two new trailing
// arguments are new versus the pre-Phase-12 call this replaces.
//
// VERIFIED (Phase 12 investigation, matching this constructor's own qApp->setCurrentScene()/
// setSceneView() comment above): actiond's main() (main.cpp) never calls
// QCoreApplication::exec() -- there is no Qt event loop running in this process, ever, in either
// one-shot or daemon mode. VUndoCommand::RedoFullParsing() (vundocommand.cpp) -- the shared base
// every mutating tool's undo command (AddToCalc, SaveToolOptions, SavePieceOptions, ...) either
// inherits or reimplements identically -- reacts to this correctly, not by accident: on a
// command's *very first* execution (redoFlag == false, i.e. the redo() Qt's own QUndoStack::
// push() calls synchronously the moment AddToFile()/SaveOption()/etc. pushes a freshly-created
// command), it takes the `QApplication::postEvent(doc, new LiteParseEvent())` branch instead of
// emitting FullUpdateFromFile() -- and that posted event is simply never delivered in this
// process, permanently. This is harmless, not a silently-broken feature: at that exact moment the
// in-memory VContainer/scene were *already* built directly by the tool's own Create() factory
// (VContainer::AddPoint(), scene->addItem(), ...) moments earlier in the same call, wholly
// independent of AddToCalc/SaveToolOptions -- nothing downstream is out of sync yet, so no
// reparse is actually needed. redoFlag is set true unconditionally right after (regardless of
// which branch ran), so on the *second and later* call to that same command's redo() -- which is
// exactly what "session.redo" (session_undo_handlers.cpp) triggers after a prior "session.undo",
// and what AddToCalc::undo() (addtocalc.cpp) does on *every* call, not just later ones -- the
// synchronous `emit NeedFullParsing(); emit doc->FullUpdateFromFile();` branch runs instead. A
// direct-connection Qt signal emission executes its slots inline, in the emitting call's own
// stack frame, with no event-loop involvement at all -- so THAT part of the mechanism (each
// already-alive VAbstractTool subclass's own FullUpdateFromFile() override re-running) does fire
// correctly in this headless daemon despite the dead posted event.
//
// CRITICAL FINDING, VERIFIED BY DIRECT actiond RUN (Phase 12 -- do not assume this away in a
// future change, see session_undo_handlers.h's own header comment for the full writeup and a
// reproduction script): that per-tool refresh does NOT delete an object whose DOM element
// "session.undo" just removed. VDrawTool::ReadAttributes() (vdrawtool.cpp) -- what every
// FullUpdateFromFile() override ultimately calls -- does `doc->elementById(m_id, getTagName())`
// and, finding nothing, just `qCWarning`s "Can't find tool with id" and returns, leaving the
// existing in-memory VPointF/tool object (and its scene item) exactly as it was. There is no
// object-deletion mechanism anywhere in this codebase yet (matching the already-documented Phase
// 9 gap: "no delete ... action existed yet for any object type") -- undo included. Net effect,
// reproduced directly: after "session.undo" empties the stack, "pattern.dump"/"render.snapshot"/
// "pattern.resolveName" still report the "undone" objects, completely unchanged, because they all
// read the same live VContainer/scene this cascade never actually prunes. "session.save"
// immediately after the same undo DOES write a correctly-reverted (smaller) DOM to disk -- the
// AddToCalc::undo() DOM removal itself is genuinely correct; only the live in-memory mirror is
// stale. This is NOT a Phase 12 regression to fix here: it is a pre-existing property of how
// undo/redo already worked (the interactive GUI has no separate "prune VContainer" step either,
// as far as this investigation traced) -- Phase 12 exposes it to a headless/AI caller for the
// first time, so it is flagged here at maximum visibility rather than silently inherited.
QJsonDocument PatternSession::runActions(const QJsonDocument &script, bool abortOnFirstError)
{
    // KNOWN GAP (documented, not silently shipped): PatternPieceTool::ToolCreation() and
    // InternalPathTool::ToolCreation() (pattern_piece_tool.cpp/internal_path_tool.cpp) each call
    // `qApp->getUndoStack()->endMacro()` unconditionally whenever typeCreation != Source::FromTool
    // -- which is always true for piece_handlers.cpp's calls (they pass Source::FromGui) -- with
    // NO matching beginMacro() of their own on that code path (only the *dialog*-based Create()
    // overloads, which this action layer never calls, open one -- see union_tool.cpp/
    // pattern_piece_tool.cpp's own beginMacro("...") call sites for the GUI-only path this isn't).
    // Before Phase 12, this was a harmless no-op: QUndoStack::endMacro() with an empty macro_stack
    // just logs "no matching beginMacro()" via qWarning and returns. Since Phase 12 wraps every
    // mutating action in its own outer beginMacro()/endMacro() (below), that inner stray
    // endMacro() call now matches and closes THIS macro instead -- the moment
    // PatternPieceTool::ToolCreation()/InternalPathTool::ToolCreation() returns, which for
    // "piece.addPatternPiece"/"piece.internalPath" is also the last DOM-mutating step in the
    // handler, so the macro still ends up containing the whole action's real mutation either way.
    // Net effect verified: the qWarning noise is actually *fixed* as a side effect (our
    // beginMacro() is now the "matching" one), and this loop's own endMutatingAction() call
    // afterward becomes a harmless second, empty-macro_stack no-op of its own. This would only
    // become a real problem if a future change to either ToolCreation() override pushed another
    // undo command *after* this inner endMacro() runs (there currently is none) -- flagged here so
    // that future change doesn't quietly break "one JSON action = one undo step" for these two ops.
    auto beginMutatingAction = [](const QString &label)
    {
        qApp->getUndoStack()->beginMacro(label);
    };
    auto endMutatingAction = []()
    {
        qApp->getUndoStack()->endMacro();
    };
    return m_engine.run(script, m_context, abortOnFirstError, beginMutatingAction, endMutatingAction);
}

bool PatternSession::save(const QString &path, QString &error)
{
    // Absolutized for the same reason the constructor absolutizes --pattern/--measurements: a
    // caller (action_host.cpp's --save-pattern flag) may reasonably pass a relative path, but
    // RelativeMPath()/AbsoluteMPath() below only produce a correct result given an absolute one.
    const QString absolutePath = QFileInfo(path).absoluteFilePath();

    // Mirrors MainWindow::SavePattern() (mainwindow.cpp): doc->MPath() was stored relative to
    // whatever qApp->getFilePath() was at load/measurements-load time, which is not necessarily
    // this save's own destination (e.g. action_host.cpp's --save-pattern, or a script's
    // session.save action, routinely save to a different path than --pattern named). Re-anchor it
    // to absolutePath here so the saved XML's <measurements> element resolves correctly no matter
    // where the file ends up, exactly as the real GUI does on every save.
    const QString mPath = AbsoluteMPath(qApp->getFilePath(), m_doc->MPath());
    if (!mPath.isEmpty() && qApp->getFilePath() != absolutePath)
    {
        m_doc->SetMPath(RelativeMPath(absolutePath, mPath));
    }

    const bool result = m_doc->SaveDocument(absolutePath, error);
    if (result)
    {
        qApp->setFilePath(absolutePath); // Mirrors MainWindow::SavePattern()'s own setCurrentFile(fileName) call, so a later save/measurements load computes correctly against it too.
    }
    else if (!mPath.isEmpty())
    {
        m_doc->SetMPath(mPath); // Save failed: restore the pre-recompute path, exactly as SavePattern()'s own failure branch does.
    }
    return result;
}

const ActionContext &PatternSession::context() const
{
    return m_context;
}
