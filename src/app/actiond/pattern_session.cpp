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
#include "../../libs/ifc/xml/vpatternconverter.h"    // Brings in VPatternConverter, which upgrades older-format pattern files.
#include "../../libs/vformat/measurements.h"         // Brings in MeasurementDoc, used to load and parse the measurement file.
#include "../../libs/vpatterndb/vcontainer.h"        // Brings in VContainer, the data container the pattern and measurements populate.
#include "../../libs/vwidgets/vmaingraphicsscene.h"  // Brings in VMainGraphicsScene, required (non-null) by VPattern::Parse()/CreateEmptyFile() callers.
#include "../../libs/vwidgets/vmaingraphicsview.h"   // Brings in VMainGraphicsView; see the qApp->setSceneView() comment below for why one is needed.
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
    // rationale of each call). A no-op when measurementsFilePath is empty -- the daemon's
    // --measurements flag (and the one-shot CLI's, after this change) is optional.
    void loadMeasurementsIfGiven(VContainer *data, const QString &measurementsFilePath)
    {
        if (measurementsFilePath.isEmpty())
        {
            return;
        }
        requireFileExists(measurementsFilePath, QStringLiteral("Measurements"));

        MeasurementDoc measurements(data);
        measurements.setSize(VContainer::rsize());
        measurements.setHeight(VContainer::rheight());
        measurements.setXMLContent(measurementsFilePath);
        qApp->setPatternType(measurements.Type());
        measurements.readMeasurements();
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

    if (patternFilePath.isEmpty())
    {
        // No pattern file given: build the minimal "new pattern" DOM directly, matching what
        // MainWindow::New()'s dialog-driven path ultimately relies on -- no draft blocks yet; the
        // first "basePoint" action's own doc->appendDraftBlock() call creates the first one.
        m_doc->CreateEmptyFile();
        loadMeasurementsIfGiven(m_data, measurementsFilePath);
    }
    else
    {
        requireFileExists(patternFilePath, QStringLiteral("Pattern"));

        // VPatternConverter upgrades an older-format .val file to the schema version
        // VPattern::Parse() expects, exactly as MainWindow::LoadPattern() does.
        VPatternConverter converter(patternFilePath);
        m_doc->setXMLContent(converter.Convert());     // Loads the (possibly just-upgraded) pattern XML into doc's DOM tree.
        qApp->setPatternUnit(m_doc->measurementUnits()); // Sync qApp's unit to the pattern file's own declared unit, as MainWindow::LoadPattern() does.

        // Measurements must be loaded before Parse() below: pattern formulas can reference
        // measurement variables, which have to already exist in data by the time they're evaluated.
        loadMeasurementsIfGiven(m_data, measurementsFilePath);

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

QJsonDocument PatternSession::runActions(const QJsonDocument &script, bool abortOnFirstError)
{
    return m_engine.run(script, m_context, abortOnFirstError);
}

bool PatternSession::save(const QString &path, QString &error)
{
    return m_doc->SaveDocument(path, error);
}

const ActionContext &PatternSession::context() const
{
    return m_context;
}
