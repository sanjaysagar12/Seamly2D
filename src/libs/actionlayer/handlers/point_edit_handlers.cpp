//---------------------------------------------------------------------------------------------------------------------
//  @file   point_edit_handlers.cpp
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

#include "point_edit_handlers.h" // Brings in the ActionResult-returning handlePointEdit declaration this file implements.

#include "../action_context.h" // Brings in ActionContext, supplying the data container this handler reads/mutates.
#include "../name_resolver.h"  // Brings in NameResolver::idForName(); ActionResolverError propagates to ActionEngine::run() uncaught, exactly like every other mutating handler.

#include "../../vpatterndb/vcontainer.h" // Brings in VContainer, passed to VFormula's constructor below.
#include "../../vpatterndb/vformula.h"   // Brings in VFormula, used to validate/build "length"/"angle" before handing them to each tool's own setter.
#include "../../ifc/xml/vabstractpattern.h" // Brings in VAbstractPattern::getTool(id), used to reach the named point's live tool instance.
#include "../../ifc/exception/vexception.h" // Brings in VException (and VExceptionBadId), thrown by getTool()/the tools' own save path.
#include "../../vmisc/def.h"               // Brings in degreeSymbol and UnitsToStr(), used to build each VFormula's postfix exactly like the tools' own GetFormulaLength()/GetFormulaAngle() do.
#include "../../vmisc/vabstractapplication.h" // Brings in the qApp macro, used for qApp->patternUnit().

// point.edit is the first op that *mutates an existing* tool rather than creating a new one, so
// (unlike every Create()-based handler) it needs the concrete tool classes' own headers for their
// already-public setters, not just a Create() factory. actionlayer.pro does not link libvtools
// itself -- only actiond.pro/ActionLayerTest.pro do, for the final executable -- so this is a
// header-only dependency, the same idiom pattern_dump_handler.cpp/render_handlers.cpp already
// rely on for vdatatool.h.
#include "../../vtools/tools/vdatatool.h"
#include "../../vtools/tools/drawTools/toolpoint/toolsinglepoint/vtoolbasepoint.h"
#include "../../vtools/tools/drawTools/toolpoint/toolsinglepoint/toollinepoint/vtoollinepoint.h"
#include "../../vtools/tools/drawTools/toolpoint/toolsinglepoint/toollinepoint/vtoolendline.h"

#include <QJsonArray>  // Provides QJsonArray, the "updated" field listing which args were actually applied.
#include <QJsonObject> // Provides QJsonObject, used for both the input args and the output payload.
#include <QPointF>     // Provides QPointF, the type VToolBasePoint::SetBasePointPos() takes.
#include <QString>     // Provides QString, used throughout for field values.

namespace
{
    // Builds the same {"type","message"} structured-error shape every other mutating handler in
    // this action layer already produces (see formula_point_handlers.cpp's own structuredError()).
    QJsonObject structuredError(const QString &type, const QString &message)
    {
        QJsonObject error;
        error["type"] = type;
        error["message"] = message;
        return error;
    }
}

// Implements "point.edit": see point_edit_handlers.h for the documented JSON shape and the exact
// scope of which point-tool types support which fields.
ActionResult handlePointEdit(const QJsonObject &args, const ActionContext &ctx)
{
    const QString name = args.value(QStringLiteral("name")).toString();
    if (name.isEmpty())
    {
        return ActionResult::failure(QStringLiteral("point.edit requires a non-empty \"name\""));
    }

    const bool hasX = args.contains(QStringLiteral("x"));
    const bool hasY = args.contains(QStringLiteral("y"));
    const bool hasLength = args.contains(QStringLiteral("length"));
    const bool hasAngle = args.contains(QStringLiteral("angle"));

    if (hasX != hasY) // Editing only one of x/y would leave the point's other coordinate meaningless -- both or neither.
    {
        return ActionResult::failure(QStringLiteral("point.edit requires \"x\" and \"y\" together, not just one"));
    }
    if (!hasX && !hasLength && !hasAngle)
    {
        return ActionResult::failure(
            QStringLiteral("point.edit requires at least one of \"x\"/\"y\", \"length\", or \"angle\""));
    }

    VContainer *data = ctx.data();
    if (data == nullptr)
    {
        return ActionResult::failure(QStringLiteral("point.edit: context is missing a data container"));
    }

    // Throws ActionResolverError on an unknown name; deliberately not caught here so
    // ActionEngine::run()'s existing structured {"type":"nameResolution",...} handling applies,
    // matching every other mutating handler's convention (see formula_point_handlers.cpp).
    const quint32 id = NameResolver::idForName(name, data, Draw::Calculation);

    VDataTool *tool = nullptr;
    try
    {
        tool = VAbstractPattern::getTool(id); // Process-wide static lookup, keyed by the id every Create()-based tool registers itself under.
    }
    catch (const VExceptionBadId &) // No tool registered for this id -- e.g. an operation-created (move/rotation/mirror) destination point, which is a plain VPointF with no tool of its own.
    {
        QJsonObject error = structuredError(QStringLiteral("toolNotFound"), QStringLiteral(
            "\"%1\" has no editable tool (it may be an operation-created point, not a drawing tool)").arg(name));
        return ActionResult::failure(QJsonValue(error));
    }

    QJsonArray updated; // Accumulates which of x/y/length/angle were actually applied, for the success payload.

    try
    {
        if (hasX) // hasX == hasY here, guaranteed by the guard above.
        {
            VToolBasePoint *basePointTool = qobject_cast<VToolBasePoint *>(tool);
            if (basePointTool == nullptr)
            {
                QJsonObject error = structuredError(QStringLiteral("unsupported"), QStringLiteral(
                    "\"%1\" is not a basePoint-created point; \"x\"/\"y\" editing is only supported there").arg(name));
                return ActionResult::failure(QJsonValue(error));
            }

            // SetBasePointPos() converts via qApp->toPixel() (the pattern's current working unit,
            // with no per-call override) internally, then saves+recomputes -- see vtoolbasepoint.cpp.
            const qreal x = args.value(QStringLiteral("x")).toDouble();
            const qreal y = args.value(QStringLiteral("y")).toDouble();
            basePointTool->SetBasePointPos(QPointF(x, y));
            updated.append(QStringLiteral("x"));
            updated.append(QStringLiteral("y"));
        }

        if (hasLength)
        {
            VToolLinePoint *linePointTool = qobject_cast<VToolLinePoint *>(tool); // Common base of endLine/alongLine/normal/bisector/shoulderPoint.
            if (linePointTool == nullptr)
            {
                QJsonObject error = structuredError(QStringLiteral("unsupported"), QStringLiteral(
                    "\"%1\" has no editable formula length (not a line-point tool: endLine/alongLine/normal/bisector/shoulderPoint)")
                    .arg(name));
                return ActionResult::failure(QJsonValue(error));
            }

            // VFormula's constructor evaluates the formula immediately, but with setCheckZero()'s
            // *default* (true) -- re-evaluating after setCheckZero(true) below is therefore
            // redundant but harmless; the explicit call documents the intent (a zero length is
            // invalid) the same way VToolLinePoint::GetFormulaLength() itself does. Unlike
            // SetFormulaLength() (which silently no-ops on an error formula -- it never reports
            // failure itself), this handler checks .error() itself so a bad formula is reported as
            // a normal action failure, not silently ignored.
            VFormula formula(args.value(QStringLiteral("length")).toString(), data);
            formula.setToolId(id);
            formula.setPostfix(UnitsToStr(qApp->patternUnit()));
            formula.setCheckZero(true);
            formula.Eval();
            if (formula.error())
            {
                QJsonObject error = structuredError(QStringLiteral("formulaError"),
                    QStringLiteral("invalid \"length\" formula for \"%1\"").arg(name));
                return ActionResult::failure(QJsonValue(error));
            }
            linePointTool->SetFormulaLength(formula);
            updated.append(QStringLiteral("length"));
        }

        if (hasAngle)
        {
            // Only VToolEndLine exposes a public formula-angle setter today; normal/bisector/
            // shoulderPoint use a plain numeric angle *offset* instead (a different field, not a
            // formula -- see formula_point_handlers.cpp's handleNormal() comment), and
            // curveIntersectAxis/lineIntersectAxis have their own formula-angle setters. See
            // point_edit_handlers.h's own comment for why extending this is a documented follow-up.
            VToolEndLine *endLineTool = qobject_cast<VToolEndLine *>(tool);
            if (endLineTool == nullptr)
            {
                QJsonObject error = structuredError(QStringLiteral("unsupported"), QStringLiteral(
                    "\"%1\" has no editable formula angle (only endLine-created points support \"angle\" editing so far)")
                    .arg(name));
                return ActionResult::failure(QJsonValue(error));
            }

            VFormula formula(args.value(QStringLiteral("angle")).toString(), data);
            formula.setToolId(id);
            formula.setPostfix(degreeSymbol);
            formula.setCheckZero(false); // A zero angle is valid, matching VToolEndLine::GetFormulaAngle()'s own contract.
            formula.Eval();
            if (formula.error())
            {
                QJsonObject error = structuredError(QStringLiteral("formulaError"),
                    QStringLiteral("invalid \"angle\" formula for \"%1\"").arg(name));
                return ActionResult::failure(QJsonValue(error));
            }
            endLineTool->SetFormulaAngle(formula);
            updated.append(QStringLiteral("angle"));
        }
    }
    // SetBasePointPos()/SetFormulaLength()/SetFormulaAngle() all funnel through VDrawTool::
    // SaveOption(), which pushes a SaveToolOptions undo command whose redo() rewrites the DOM and
    // triggers doc->LiteParseTree() -- reachable failure paths (e.g. a downstream point's own
    // formula no longer evaluating once this point moved) throw VException, exactly like every
    // Create()-based handler's own runCreate()-equivalent guard.
    catch (const VException &error)
    {
        return ActionResult::failure(error.ErrorMessage());
    }
    catch (const std::exception &error)
    {
        return ActionResult::failure(QString::fromUtf8(error.what()));
    }
    catch (...)
    {
        return ActionResult::failure(QStringLiteral("point.edit: unknown error updating \"%1\"").arg(name));
    }

    QJsonObject payload;
    payload["id"] = static_cast<qint64>(id);
    payload["name"] = name;
    payload["updated"] = updated;
    return ActionResult::success(payload);
}
