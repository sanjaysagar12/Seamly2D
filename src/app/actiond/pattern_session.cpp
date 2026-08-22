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
    , m_pieceLayoutCursor() // Default-constructed: starts empty, accumulates as piece.addPatternPiece auto-places pieces over this session's lifetime.
    , m_context(m_draftScene, m_doc.data(), m_data, m_pieceScene, &m_pieceLayoutCursor)
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

// Phase 12 (second design; see history_undo_handlers.h's own header comment for why the earlier
// QUndoStack-based session.undo/session.redo/session.undoStatus was abandoned in favor of this
// history/DOM-based "pattern.undo"): handlePatternUndo() (history_undo_handlers.cpp) removes DOM
// elements via DelTool/DeletePiece/DeleteDraftBlock's own redo(), but -- confirmed by reading
// VPattern::PrepareForParse() -- only a genuine VPattern::Parse(Document::FullParse) actually
// clears and rebuilds VContainer/the scenes/doc->getHistory() from the (now-shorter) DOM;
// VAbstractPattern::LiteParseTree() (the one reparse entry point reachable from inside
// actionlayer's own handlers, which only ever see a VAbstractPattern*) explicitly refuses
// Document::FullParse ("Lite parsing doesn't support full parsing" -- vpattern.cpp). Only
// VPattern::Parse() itself can do a real FullParse, and VPattern::Parse() is declared on VPattern,
// not VAbstractPattern (see measurements_sync_handlers.cpp's own comment on the same boundary) --
// so only PatternSession, which owns the real VPattern instance (m_doc, below), can reach it.
//
// VERIFIED THE HARD WAY (real actiond run during development, not assumed): the first version of
// this method scanned the WHOLE finished "results" array once, after m_engine.run() returned, and
// reparsed only then. That is wrong, and was caught immediately by a same-batch test: a script
// running "pattern.undo" followed by "pattern.dump" *in the same actions array* still saw the
// stale (pre-reparse) state in that dump, because the reparse hadn't happened yet at the point
// that later action was dispatched -- reproducing, inside one script, exactly the class of
// staleness bug the abandoned QUndoStack design suffered from across whole processes. The fix is
// ActionEngine::run()'s new AfterActionFn hook (action_engine.h): it fires immediately after each
// action's own result is recorded, before the loop advances to the next action, so the reparse
// below runs between "pattern.undo" and whatever comes right after it in the same script, not once
// at the very end.
//
// Every other mutating op needs no equivalent step: object CREATION already mutates VContainer/
// the scene directly inside its own Create() call (see e.g. point_handlers.cpp's handleBasePoint()),
// with no reparse required -- this is specific to DOM-only deletion, which is new in Phase 12.
QJsonDocument PatternSession::runActions(const QJsonDocument &script, bool abortOnFirstError)
{
    auto afterAction = [this](const QString &op, bool ok)
    {
        if (ok && op == QStringLiteral("pattern.undo"))
        {
            m_doc->Parse(Document::FullParse); // Rebuilds VContainer/both scenes/doc->getHistory() from the DOM handlePatternUndo() just edited -- see this method's own comment above for why this specific call, here, is required.
        }
    };
    return m_engine.run(script, m_context, abortOnFirstError, afterAction);
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
