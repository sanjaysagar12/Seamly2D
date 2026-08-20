//---------------------------------------------------------------------------------------------------------------------
//  @file   line_handlers.cpp
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

#include "line_handlers.h" // Brings in the ActionResult-returning handleLine declaration this file implements.

#include "../action_context.h" // Brings in ActionContext, supplying scene/doc/data for this handler.
#include "../name_resolver.h"  // Brings in NameResolver::idForName(); ActionResolverError propagates to ActionEngine::run() uncaught, by design.

#include "../../vpatterndb/vcontainer.h"  // Brings in VContainer: GetGObject(), the data container VToolLine::Create() mutates.
#include "../../vgeometry/vgobject.h"     // Brings in VGObject::getType(), used for the point-type guard below.
#include "../../vgeometry/vgeometrydef.h" // Brings in the GOType enum classified by the point-type guard below.
#include "../../ifc/xml/vabstractpattern.h" // Brings in VAbstractPattern (for the Document enum) and the type VToolLine::Create() mutates.
#include "../../ifc/exception/vexception.h" // Brings in VException (and subclasses), the type every reused Seamly2D tool-creation call throws on failure.
#include "../../ifc/ifcdef.h"               // Brings in LineTypeSolidLine/DefaultLineWeight/ColorBlack, DialogLine's own ultimate fallback defaults.
#include "../../vtools/tools/drawTools/vtoolline.h" // Brings in VToolLine::Create(), this op's actual creation recipe.
#include "../../vwidgets/vmaingraphicsscene.h"      // Brings in VMainGraphicsScene, required (non-null) by VToolLine::Create().

#include <QJsonObject> // Provides QJsonObject, used for both the input args and the output payload.
#include <QSharedPointer> // Provides QSharedPointer, the storage type GetGObject() returns.
#include <QString>     // Provides QString, used throughout for field values.

namespace
{
    // Returns a failure ActionResult if id does not name a Point object, or a null (success)
    // QString otherwise. Guards a real crash risk: VToolLine::Create() internally calls
    // VContainer::GeometricObject<VPointF>(id), whose only protection against a same-id,
    // wrong-type object is Q_ASSERT (SCASSERT) -- compiled to nothing in this project's release
    // build (V_NO_ASSERT/NDEBUG are defined; see actionlayer.pro). A JSON action can name any
    // existing object by name, so this handler cannot assume the id NameResolver returns is
    // actually a point the way every hand-written internal caller of VToolLine::Create() can.
    QString checkIsPoint(const VContainer *data, quint32 id, const QString &fieldName, const QString &name)
    {
        const QSharedPointer<VGObject> obj = data->GetGObject(id); // Untyped accessor; no dynamic_cast/assert risk, unlike GeometricObject<T>().
        if (obj.isNull() || obj->getType() != GOType::Point)
        {
            return QStringLiteral("\"%1\" (\"%2\") does not name a point").arg(fieldName, name);
        }
        // Defense-in-depth: id was resolved via NameResolver::idForName(..., Draw::Calculation)
        // above, which should already make a non-Calculation object impossible here -- but this
        // is a cheap, load-bearing check against a future call site that reintroduces the
        // unscoped idForName() overload by mistake (see name_resolver.h's own comment on why a
        // same-named Draw::Modeling piece-node clone can otherwise be resolved instead).
        if (obj->getMode() != Draw::Calculation)
        {
            return QStringLiteral("\"%1\" (\"%2\") resolved to a %3 object, not a calculation-context point")
                .arg(fieldName, name, NameResolver::drawModeToString(obj->getMode()));
        }
        return QString(); // Empty string signals "no problem found".
    }
}

// Implements "line": {"firstPoint","secondPoint","lineType"?,"lineWeight"?,"lineColor"?} ->
// {"id": <new tool id>, "firstPoint": <name>, "secondPoint": <name>}.
//
// lineType/lineWeight/lineColor default to LineTypeSolidLine/DefaultLineWeight/ColorBlack --
// traced from DialogLine's constructor (dialogline.cpp) through
// VAbstractPattern::getDefaultLine{Type,Weight,Color}() to VCommonSettings::getDefaultLine{...}(),
// whose own QSettings-missing fallbacks are literally "solidLine" / 0.35 / "black": exactly the
// three ifcdef.cpp constants used here, so a fresh install's dialog defaults and this handler's
// JSON-omitted defaults are identical without copy-pasting the literals a second time.
ActionResult handleLine(const QJsonObject &args, const ActionContext &ctx)
{
    const QString firstName = args.value(QStringLiteral("firstPoint")).toString();   // Name of the line's first endpoint.
    const QString secondName = args.value(QStringLiteral("secondPoint")).toString(); // Name of the line's second endpoint.
    if (firstName.isEmpty() || secondName.isEmpty()) // Both endpoints are mandatory; there is no sane default.
    {
        return ActionResult::failure(QStringLiteral("line requires non-empty \"firstPoint\" and \"secondPoint\""));
    }

    VContainer *data = ctx.data();          // Local alias for the pattern's variable/data container.
    VAbstractPattern *doc = ctx.doc();      // Local alias for the pattern document; required for AddToFile()'s DOM write.
    VMainGraphicsScene *scene = ctx.scene(); // Local alias for the draft scene; VToolLine::Create() asserts this is non-null.
    if (data == nullptr || doc == nullptr || scene == nullptr) // Guard against a context missing any of the three pieces this op mutates.
    {
        return ActionResult::failure(QStringLiteral("line: context is missing a scene, document, or data container"));
    }

    // Throws ActionResolverError on an unknown name; deliberately not caught here so
    // ActionEngine::run()'s existing Phase 4 catch clause serializes it into the same structured
    // {"type":"nameResolution",...} shape "pattern.resolveName" already produces, instead of this
    // handler inventing a second, differently-shaped "unknown point" error.
    const quint32 firstId = NameResolver::idForName(firstName, data, Draw::Calculation);
    const quint32 secondId = NameResolver::idForName(secondName, data, Draw::Calculation);

    const QString firstTypeError = checkIsPoint(data, firstId, QStringLiteral("firstPoint"), firstName);   // See checkIsPoint()'s comment for why this guard exists.
    if (!firstTypeError.isEmpty())
    {
        return ActionResult::failure(firstTypeError);
    }
    const QString secondTypeError = checkIsPoint(data, secondId, QStringLiteral("secondPoint"), secondName);
    if (!secondTypeError.isEmpty())
    {
        return ActionResult::failure(secondTypeError);
    }

    const QString lineType = args.contains(QStringLiteral("lineType"))
                                  ? args.value(QStringLiteral("lineType")).toString()
                                  : LineTypeSolidLine; // DialogLine's own ultimate fallback; see the function comment above.
    const QString lineWeight = args.contains(QStringLiteral("lineWeight"))
                                    ? args.value(QStringLiteral("lineWeight")).toString()
                                    : DefaultLineWeight; // DialogLine's own ultimate fallback; see the function comment above.
    const QString lineColor = args.contains(QStringLiteral("lineColor"))
                                   ? args.value(QStringLiteral("lineColor")).toString()
                                   : ColorBlack; // DialogLine's own ultimate fallback; see the function comment above.

    try
    {
        // _id=0 (unknown until VContainer::getNextId() assigns one inside Create()),
        // Document::FullParse (build the real scene item + history record), Source::FromGui (this
        // is new pattern data introduced right now, not a reload of something already in the DOM,
        // so AddToFile() must run and actually write the <line> element).
        VToolLine *tool = VToolLine::Create(0, firstId, secondId, lineType, lineWeight, lineColor, scene, doc, data,
                                             Document::FullParse, Source::FromGui);
        if (tool == nullptr) // Create() can return nullptr for a parse mode other than FullParse; defensive guard, should not happen here.
        {
            return ActionResult::failure(QStringLiteral("line: tool creation failed for an unknown reason"));
        }

        QJsonObject payload;                             // Builds the documented {"id","firstPoint","secondPoint"} result.
        payload["id"] = static_cast<qint64>(tool->getId()); // Widen quint32 losslessly into qint64 for JSON.
        payload["firstPoint"] = firstName;                  // Echoes the input names back, for a caller that only kept the JSON action around.
        payload["secondPoint"] = secondName;
        return ActionResult::success(payload);
    }
    catch (const VException &error) // Every reused Seamly2D tool-creation call (VToolLine::Create, AddToFile's undo push) throws this family.
    {
        return ActionResult::failure(error.ErrorMessage()); // Seamly2D's own human-readable message for the failure.
    }
    catch (const std::exception &error) // Catches anything else well-behaved that isn't a VException.
    {
        return ActionResult::failure(QString::fromUtf8(error.what()));
    }
    catch (...) // Absolute last resort: guarantees no exception of any kind escapes this handler uncaught.
    {
        return ActionResult::failure(QStringLiteral("line: unknown error creating the line"));
    }
}
