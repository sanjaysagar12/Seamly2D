//---------------------------------------------------------------------------------------------------------------------
//  @file   action_host.cpp
//  @author Seamly2D Contributors
//  @date   19 Aug, 2026
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

#include "action_host.h" // Brings in the ActionHost::runActions declaration this file implements.

#include "../seamly2d/xml/vpattern.h" // Brings in VPattern, the same VAbstractPattern subclass seamly2d itself uses to parse .val files.

#include "../../libs/ifc/exception/vexception.h"     // Brings in VException, thrown below for every load-time failure.
#include "../../libs/ifc/xml/vpatternconverter.h"    // Brings in VPatternConverter, which upgrades older-format pattern files.
#include "../../libs/vformat/measurements.h"         // Brings in MeasurementDoc, used to load and parse the measurement file.
#include "../../libs/vpatterndb/vcontainer.h"        // Brings in VContainer, the data container the pattern and measurements populate.
#include "../../libs/vwidgets/vmaingraphicsscene.h"  // Brings in VMainGraphicsScene, required (non-null) by VPattern::Parse().
#include "../../libs/vmisc/vabstractapplication.h"   // Brings in the qApp accessors used to mirror MainWindow::LoadPattern()'s setup.

#include "../../libs/actionlayer/action_context.h"  // Brings in ActionContext, bundling scene/doc/data for the engine.
#include "../../libs/actionlayer/action_registry.h" // Brings in ActionRegistry, auto-registering Phase 1's built-in handlers.
#include "../../libs/actionlayer/action_engine.h"   // Brings in ActionEngine::run(), the actual script dispatcher.

#include <QFileInfo>       // Provides QFileInfo::exists(), used for the fast, clear existence checks below.
#include <QScopedPointer>  // Provides QScopedPointer, giving doc RAII cleanup without a manual delete.

namespace
{
    // Throws a VException with a clear message if the given file does not exist. Checking this
    // up front means a missing file is reported as "file not found", not as a confusing deep XML
    // parser error from inside VPatternConverter/MeasurementDoc.
    void requireFileExists(const QString &filePath, const QString &kind)
    {
        if (!QFileInfo::exists(filePath)) // A single, cheap stat call; cheaper than attempting to open and parse first.
        {
            throw VException(QStringLiteral("%1 file not found: %2").arg(kind, filePath)); // Same exception type every other load failure below uses.
        }
    }
}

namespace ActionHost
{
    QJsonDocument runActions(const QString &patternFilePath, const QString &measurementsFilePath,
                              const QJsonDocument &actionsScript)
    {
        requireFileExists(patternFilePath, QStringLiteral("Pattern"));           // Fail fast with a clear message rather than a parser error.
        requireFileExists(measurementsFilePath, QStringLiteral("Measurements")); // Fail fast with a clear message rather than a parser error.

        // Data container the pattern and its measurements populate. Mirrors
        // MainWindowsNoGUI's own construction: qApp->translateVariables() supplies formula-token
        // translation, and qApp->patternUnitP() gives a live pointer to qApp's own unit storage,
        // so later qApp->setPatternUnit() calls below are automatically visible through it.
        VContainer data(qApp->translateVariables(), qApp->patternUnitP());

        // VPattern::Parse() asserts both scenes are non-null and vtools' per-tool Create()
        // factories add graphics items to them while parsing; a real (if display-less) scene is
        // required, not optional, even though actiond never renders anything to a window.
        VMainGraphicsScene draftScene; // Draft-mode scene: base points, lines, curves, etc.
        VMainGraphicsScene pieceScene; // Piece-mode scene: cut pieces built from the draft.

        // QScopedPointer gives doc RAII cleanup on every return path (including the exceptions
        // thrown by setXMLContent()/Parse() below) without a manual try/catch-and-delete.
        QScopedPointer<VPattern> doc(new VPattern(&data, &draftScene, &pieceScene));
        qApp->setCurrentDocument(doc.data()); // Mirrors MainWindow's own setup; some formula/tool code reaches for qApp's "current" document.
        qApp->setCurrentData(&data);          // Mirrors MainWindow's own setup; some formula/tool code reaches for qApp's "current" data container.

        // VPatternConverter upgrades an older-format .val file to the schema version
        // VPattern::Parse() expects, exactly as MainWindow::LoadPattern() does; reusing it avoids
        // reimplementing pattern-format version migration here.
        VPatternConverter converter(patternFilePath);
        doc->setXMLContent(converter.Convert()); // Loads the (possibly just-upgraded) pattern XML into doc's DOM tree.
        qApp->setPatternUnit(doc->measurementUnits()); // Sync qApp's unit to the pattern file's own declared unit, as MainWindow::LoadPattern() does.

        // Measurements must be loaded before Parse() below: pattern formulas can reference
        // measurement variables, which have to already exist in data by the time they're evaluated.
        MeasurementDoc measurements(&data);
        measurements.setSize(VContainer::rsize());        // Matches MainWindow::LoadPattern()'s setup for gradation-aware formulas.
        measurements.setHeight(VContainer::rheight());     // Matches MainWindow::LoadPattern()'s setup for gradation-aware formulas.
        measurements.setXMLContent(measurementsFilePath); // Loads the measurement file's XML into its own DOM tree.
        measurements.readMeasurements();                   // Parses that XML into real measurement variables inside data.

        // The real, full parse: walks the pattern XML and builds every geometry object and the
        // tool history into data/doc, via the same vtools Create() factories the GUI editor uses.
        doc->Parse(Document::FullParse);

        ActionContext ctx(&draftScene, doc.data(), &data); // Bundles the trio ActionEngine's handlers read from.
        ActionRegistry registry;                            // Auto-registers Phase 1's read-only handlers (pattern.dump, etc.).
        ActionEngine engine(registry);                       // Dispatches the script below through that registry.

        return engine.run(actionsScript, ctx); // Runs every action in the script; returns {"results": [...]}.
    }
}
