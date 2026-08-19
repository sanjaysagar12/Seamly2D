//---------------------------------------------------------------------------------------------------------------------
//  @file   point_handlers.cpp
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

#include "point_handlers.h" // Brings in the ActionResult-returning handleBasePoint declaration this file implements.

#include "../action_context.h" // Brings in ActionContext, supplying scene/doc/data for this handler.

#include "../../vpatterndb/vcontainer.h" // Brings in VContainer, the data container VToolBasePoint::Create() mutates.
#include "../../vgeometry/vpointf.h"     // Brings in VPointF, the point object this handler constructs.
#include "../../vmisc/def.h"             // Brings in Unit, Source, StrToUnits(), and the free ToPixel(qreal, Unit) conversion function.
#include "../../ifc/xml/vabstractpattern.h" // Brings in VAbstractPattern: Document enum, appendDraftBlock(), draftBlockNameExists().
#include "../../ifc/exception/vexception.h" // Brings in VException (and subclasses), the type every reused Seamly2D tool-creation call throws on failure.
#include "../../vtools/tools/drawTools/toolpoint/toolsinglepoint/vtoolbasepoint.h" // Brings in VToolBasePoint::Create(), this op's actual creation recipe.
#include "../../vwidgets/vmaingraphicsscene.h" // Brings in VMainGraphicsScene, required (non-null) by VToolBasePoint::Create().

#include <QJsonObject> // Provides QJsonObject, used for both the input args and the output payload.
#include <QString>     // Provides QString, used throughout for field values.

namespace
{
    // Builds a structured {"type", "message", ...extra} error object, mirroring the shape
    // ActionEngine::run() already produces for ActionResolverError (Phase 4) -- giving an
    // automated (AI) caller the same kind of typed, self-correcting error for this failure mode
    // instead of a plain message string.
    QJsonObject structuredError(const QString &type, const QString &message)
    {
        QJsonObject error;       // Builds the shared {"type","message"} shape every structured error here uses.
        error["type"] = type;       // Stable, machine-readable category for this error family.
        error["message"] = message; // Human-readable summary.
        return error;                // Return the populated object; callers add op-specific fields before wrapping in ActionResult::failure().
    }
}

// Implements "basePoint": {"name","x","y","mx"?,"my"?,"draftBlock","unit"?} ->
// {"id": <new tool id>, "name": <name>}.
//
// This mirrors MainWindow::addDraftBlock()'s real GUI recipe: a VToolBasePoint is Seamly2D's
// model of "the point that anchors a brand-new pattern piece", not "one more point inside an
// existing piece" -- VToolBasePoint::AddToFile() unconditionally appends a new <draftBlock>
// element, with no merge-into-existing-block logic. So "draftBlock" must NOT already exist (the
// inverse of a first read of the JSON shape might suggest); doc->appendDraftBlock() is the exact
// check-and-register step MainWindow itself calls before creating the point, and is reused here
// verbatim rather than re-implemented, so this handler's "does it already exist" rule can never
// drift from the GUI's own rule.
ActionResult handleBasePoint(const QJsonObject &args, const ActionContext &ctx)
{
    const QString name = args.value("name").toString(); // The new point's user-visible name.
    if (name.isEmpty()) // A blank name would violate VContainer::uniqueNames' non-empty expectations downstream.
    {
        return ActionResult::failure(QStringLiteral("basePoint requires a non-empty \"name\""));
    }

    if (!args.contains(QStringLiteral("x")) || !args.contains(QStringLiteral("y"))) // Both coordinates are mandatory; there is no sane default position.
    {
        return ActionResult::failure(QStringLiteral("basePoint requires both \"x\" and \"y\""));
    }
    const double xInput = args.value(QStringLiteral("x")).toDouble(); // Raw input coordinate, still in whatever unit "unit" (or its mm default) names.
    const double yInput = args.value(QStringLiteral("y")).toDouble(); // Raw input coordinate, still in whatever unit "unit" (or its mm default) names.
    const double mx = args.value(QStringLiteral("mx")).toDouble(0.0); // Label x-offset; defaults to 0 exactly like DialogSinglePoint's own default.
    const double my = args.value(QStringLiteral("my")).toDouble(0.0); // Label y-offset; defaults to 0 exactly like DialogSinglePoint's own default.

    const QString draftBlock = args.value(QStringLiteral("draftBlock")).toString(); // The new draft block's name; see the function comment above for why it must be new.
    if (draftBlock.isEmpty())
    {
        return ActionResult::failure(QStringLiteral("basePoint requires a non-empty \"draftBlock\""));
    }

    VContainer *data = ctx.data();          // Local alias for the pattern's variable/data container.
    VAbstractPattern *doc = ctx.doc();      // Local alias for the pattern document; required for AddToFile()'s DOM write.
    VMainGraphicsScene *scene = ctx.scene(); // Local alias for the draft scene; VToolBasePoint::Create() asserts this is non-null.
    if (data == nullptr || doc == nullptr || scene == nullptr) // Guard against a context missing any of the three pieces this op mutates.
    {
        return ActionResult::failure(QStringLiteral("basePoint: context is missing a scene, document, or data container"));
    }

    // qApp->toPixel(val) always converts using whatever unit the CURRENTLY LOADED pattern
    // declares (its single argument is just the value; there is no per-call unit override) -- so
    // it cannot, by itself, honor an explicit JSON "unit" field that names a different unit than
    // the loaded pattern's own. Calling the free ToPixel(val, unit) function with an explicit
    // Unit (Mm by default, or whatever "unit" names) is the unit-aware equivalent: for a pattern
    // whose own declared unit is mm (as this action layer's fixtures use), ToPixel(val, Unit::Mm)
    // is bit-for-bit identical to qApp->toPixel(val), while also correctly handling a JSON
    // "unit": "cm"/"inch" override that qApp->toPixel() alone could never express.
    const Unit unit = args.contains(QStringLiteral("unit"))
                           ? StrToUnits(args.value(QStringLiteral("unit")).toString()) // Explicit unit requested by the caller.
                           : Unit::Mm;                                                // "assume mm unless a unit field is present".
    const qreal xPixels = ToPixel(xInput, unit); // Converts the input coordinate into the internal pixel space every VPointF stores.
    const qreal yPixels = ToPixel(yInput, unit); // Converts the input coordinate into the internal pixel space every VPointF stores.

    // appendDraftBlock() both performs the existence check and (on success) marks this name as
    // the document's active draft block -- the same two-in-one step MainWindow::addDraftBlock()
    // performs before constructing its VPointF, so VDrawTool::AddRecord()'s history entry (which
    // reads doc->getActiveDraftBlockName(), a separate concept from the string VToolBasePoint::
    // Create() merely stores on the tool) ends up correct.
    if (!doc->appendDraftBlock(draftBlock))
    {
        QJsonObject error = structuredError(QStringLiteral("draftBlockExists"),
            QStringLiteral("Draft block already exists (or is empty): %1").arg(draftBlock)); // appendDraftBlock() also rejects an empty name, already excluded above.
        error["draftBlock"] = draftBlock; // Echoes the offending name for a self-correcting, automated caller.
        return ActionResult::failure(QJsonValue(error));
    }

    try
    {
        // VToolBasePoint::Create() (Source::FromGui path) takes ownership of this raw pointer via
        // VContainer::AddGObject(), so no manual delete is needed on the success path below; every
        // return before this point happens before the allocation, so no failure path can leak it.
        VPointF *point = new VPointF(xPixels, yPixels, name, mx, my);

        // _id=0 (unknown until AddGObject() assigns one), Document::FullParse (build the real
        // scene item + history record, not just update in-memory data), Source::FromGui (this is
        // new pattern data introduced right now -- not a reload of something already in the DOM --
        // so AddToFile() must run and actually write the <draftBlock>/<point> elements).
        VToolBasePoint *tool = VToolBasePoint::Create(0, draftBlock, point, scene, doc, data, Document::FullParse,
                                                       Source::FromGui);
        if (tool == nullptr) // Create() can return nullptr for a parse mode other than FullParse; defensive guard, should not happen here.
        {
            return ActionResult::failure(QStringLiteral("basePoint: tool creation failed for an unknown reason"));
        }

        QJsonObject payload;                             // Builds the documented {"id","name"} result.
        payload["id"] = static_cast<qint64>(tool->getId()); // Widen quint32 losslessly into qint64 for JSON.
        payload["name"] = name;                             // Echoes the name back, for a caller that only kept the JSON action around.
        return ActionResult::success(payload);
    }
    catch (const VException &error) // Every reused Seamly2D tool-creation call (VToolBasePoint::Create, AddToFile's undo push) throws this family.
    {
        return ActionResult::failure(error.ErrorMessage()); // Seamly2D's own human-readable message for the failure.
    }
    catch (const std::exception &error) // Catches anything else well-behaved that isn't a VException.
    {
        return ActionResult::failure(QString::fromUtf8(error.what()));
    }
    catch (...) // Absolute last resort: guarantees no exception of any kind escapes this handler uncaught.
    {
        return ActionResult::failure(QStringLiteral("basePoint: unknown error creating the base point"));
    }
}
