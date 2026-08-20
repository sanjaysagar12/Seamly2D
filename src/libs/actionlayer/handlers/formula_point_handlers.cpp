//---------------------------------------------------------------------------------------------------------------------
//  @file   formula_point_handlers.cpp
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

#include "formula_point_handlers.h" // Brings in the six ActionResult-returning handle* declarations this file implements.

#include "../action_context.h" // Brings in ActionContext, supplying scene/doc/data for every handler below.
#include "../name_resolver.h"  // Brings in NameResolver::idForName(); ActionResolverError propagates to ActionEngine::run() uncaught, exactly like line_handlers.cpp.

#include "../../vpatterndb/vcontainer.h"  // Brings in VContainer: GetGObject(), the data container every Create() below mutates.
#include "../../vgeometry/vgobject.h"     // Brings in VGObject::getType(), used by the point-type guard below.
#include "../../vgeometry/vgeometrydef.h" // Brings in the GOType enum classified by the point-type guard below.
#include "../../ifc/xml/vabstractpattern.h" // Brings in VAbstractPattern (for the Document enum) and the type every Create() below mutates.
#include "../../ifc/exception/vexception.h" // Brings in VException (and subclasses), the type every reused Seamly2D tool-creation call throws on failure.
#include "../../ifc/ifcdef.h"               // Brings in LineTypeSolidLine/DefaultLineWeight/ColorBlack, DialogLine's own ultimate fallback defaults (see line_handlers.cpp).
#include "../../qmuparser/qmuparsererror.h" // Brings in qmu::QmuParserError: VAbstractTool::CheckFormula() (called inside every Create() below) rethrows this uncaught whenever qApp->isAppInGUIMode() is false, which it always is in this headless daemon -- see the comment on runCreate() below.
#include "../../vwidgets/vmaingraphicsscene.h" // Brings in VMainGraphicsScene, required (non-null) by every Create() below.

// The six tool headers, one per op this file implements.
#include "../../vtools/tools/drawTools/toolpoint/toolsinglepoint/toollinepoint/vtoolendline.h"
#include "../../vtools/tools/drawTools/toolpoint/toolsinglepoint/toollinepoint/vtoolalongline.h"
#include "../../vtools/tools/drawTools/toolpoint/toolsinglepoint/toollinepoint/vtoolnormal.h"
#include "../../vtools/tools/drawTools/toolpoint/toolsinglepoint/toollinepoint/vtoolbisector.h"
#include "../../vtools/tools/drawTools/toolpoint/toolsinglepoint/toollinepoint/vtoolshoulderpoint.h"
#include "../../vtools/tools/drawTools/toolpoint/toolsinglepoint/vtoollineintersect.h"

#include <QJsonObject> // Provides QJsonObject, used for both the input args and the output payload.
#include <QSharedPointer> // Provides QSharedPointer, the storage type GetGObject() returns.
#include <QString>     // Provides QString, used throughout for field values.

namespace
{
    // Returns a failure ActionResult if id does not name a Point object, or a null (success)
    // QString otherwise. Duplicated from line_handlers.cpp's own local helper of the same name
    // rather than shared, matching this codebase's existing per-file-anonymous-namespace
    // convention (see point_handlers.cpp's structuredError() for the same pattern). Guards the
    // same real crash risk documented there: every Create() below internally calls
    // VContainer::GeometricObject<VPointF>(id) via an SCASSERT-only type check, compiled to
    // nothing in this project's release build (V_NO_ASSERT/NDEBUG; see actionlayer.pro), so a
    // same-id-wrong-type object would otherwise be silently mis-cast instead of caught here.
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

    // Builds the same {"type","message",...extra} structured-error shape ActionEngine::run()
    // already produces for ActionResolverError, and point_handlers.cpp's structuredError()
    // produces for "draftBlockExists" -- so a "formulaError" from this file looks like every
    // other structured error an automated (AI) caller already knows how to parse, instead of
    // inventing a differently-shaped envelope for just this file.
    QJsonObject structuredError(const QString &type, const QString &message)
    {
        QJsonObject error;
        error["type"] = type;
        error["message"] = message;
        return error;
    }

    // Runs `create` (a zero-argument callable wrapping one VTool*::Create() call) and converts
    // every failure mode it can produce into an ActionResult, so no exception of any kind ever
    // escapes a handler in this file.
    //
    // The qmu::QmuParserError clause is the one that matters most here: VAbstractTool::
    // CheckFormula() (src/libs/vtools/tools/vabstracttool.cpp) is called *inside* every Create()
    // below to evaluate each formula string against data->DataVariables(). On a bad formula it
    // catches qmu::QmuParserError internally, and only shows its interactive "fix the formula"
    // dialog `if (qApp->isAppInGUIMode())` -- in actiond that is always false (there is no
    // interactive dialog to show at all), so CheckFormula()'s own `else { throw; }` branch fires
    // and the *same* qmu::QmuParserError propagates back out of Create() uncaught. Without this
    // catch clause, that exception would escape this handler, escape ActionEngine::run() (which
    // only catches ActionResolverError), and terminate the whole actiond process on its default
    // uncaught-exception path -- taking down every other in-flight/queued action with it. Catching
    // it here, at the one boundary every formula-bearing op in this file shares, is what keeps a
    // single bad formula a per-action JSON failure instead of a process crash.
    template <class CreateFn>
    ActionResult runCreate(const QString &op, CreateFn create)
    {
        try
        {
            return create();
        }
        catch (const qmu::QmuParserError &error) // See the function comment above: this is the expected, reachable failure mode for a bad formula.
        {
            QJsonObject detail = structuredError(QStringLiteral("formulaError"),
                QStringLiteral("Formula error: %1").arg(error.GetMsg()));
            detail["op"] = op;              // Which action this failure belongs to, for a caller running a multi-action batch.
            detail["message"] = error.GetMsg();  // qmu's own human-readable diagnostic.
            detail["expr"] = error.GetExpr();     // The specific sub-expression qmu was evaluating when it failed.
            return ActionResult::failure(QJsonValue(detail));
        }
        catch (const VException &error) // Every reused Seamly2D tool-creation call (AddToFile's undo push, etc.) throws this family.
        {
            return ActionResult::failure(error.ErrorMessage()); // Seamly2D's own human-readable message for the failure.
        }
        catch (const std::exception &error) // Catches anything else well-behaved that isn't a VException or a QmuParserError.
        {
            return ActionResult::failure(QString::fromUtf8(error.what()));
        }
        catch (...) // Absolute last resort: guarantees no exception of any kind escapes this handler uncaught.
        {
            return ActionResult::failure(QStringLiteral("%1: unknown error creating the point").arg(op));
        }
    }

    // Shared field parsing every one of the six ops below needs: the new point's own name, its
    // label offset (mx/my, both default 0 exactly like DialogSinglePoint's own default -- see
    // point_handlers.cpp's basePoint handler), and its showPointName flag (default true, since
    // that is what every one of these tools' own GUI dialogs default a freshly created point to).
    struct CommonPointArgs
    {
        QString name;
        qreal mx;
        qreal my;
        bool showPointName;
    };

    CommonPointArgs parseCommonPointArgs(const QJsonObject &args)
    {
        CommonPointArgs result;
        result.name = args.value(QStringLiteral("name")).toString();
        result.mx = args.value(QStringLiteral("mx")).toDouble(0.0);
        result.my = args.value(QStringLiteral("my")).toDouble(0.0);
        result.showPointName = args.value(QStringLiteral("showPointName")).toBool(true);
        return result;
    }

    // Shared lineType/lineWeight/lineColor defaults, identical to line_handlers.cpp's handleLine()
    // (see that file's own comment for the DialogLine -> VCommonSettings trace establishing these
    // exact three literals as the real GUI's own fallback values).
    QString lineTypeOrDefault(const QJsonObject &args)
    {
        return args.contains(QStringLiteral("lineType")) ? args.value(QStringLiteral("lineType")).toString()
                                                           : LineTypeSolidLine;
    }
    QString lineWeightOrDefault(const QJsonObject &args)
    {
        return args.contains(QStringLiteral("lineWeight")) ? args.value(QStringLiteral("lineWeight")).toString()
                                                             : DefaultLineWeight;
    }
    QString lineColorOrDefault(const QJsonObject &args)
    {
        return args.contains(QStringLiteral("lineColor")) ? args.value(QStringLiteral("lineColor")).toString()
                                                            : ColorBlack;
    }
}

// Implements "endLine": see formula_point_handlers.h for the documented JSON shape.
ActionResult handleEndLine(const QJsonObject &args, const ActionContext &ctx)
{
    const CommonPointArgs common = parseCommonPointArgs(args);
    if (common.name.isEmpty())
    {
        return ActionResult::failure(QStringLiteral("endLine requires a non-empty \"name\""));
    }

    const QString basePointName = args.value(QStringLiteral("basePoint")).toString();
    if (basePointName.isEmpty())
    {
        return ActionResult::failure(QStringLiteral("endLine requires a non-empty \"basePoint\""));
    }
    if (!args.contains(QStringLiteral("length"))) // No sane default for "how far", unlike lineType/mx/my below.
    {
        return ActionResult::failure(QStringLiteral("endLine requires a \"length\" formula"));
    }

    VContainer *data = ctx.data();
    VAbstractPattern *doc = ctx.doc();
    VMainGraphicsScene *scene = ctx.scene();
    if (data == nullptr || doc == nullptr || scene == nullptr)
    {
        return ActionResult::failure(QStringLiteral("endLine: context is missing a scene, document, or data container"));
    }

    // Throws ActionResolverError on an unknown name; deliberately not caught here so
    // ActionEngine::run()'s existing Phase 4 catch clause serializes it into the same structured
    // {"type":"nameResolution",...} shape every other handler's unresolved name already produces.
    const quint32 basePointId = NameResolver::idForName(basePointName, data, Draw::Calculation);
    const QString typeError = checkIsPoint(data, basePointId, QStringLiteral("basePoint"), basePointName);
    if (!typeError.isEmpty())
    {
        return ActionResult::failure(typeError);
    }

    // Local, non-const copies: VToolEndLine::Create() takes both formulas by QString& (it may
    // rewrite them via its own interactive fix-up path, never exercised here -- see runCreate()'s
    // comment on qApp->isAppInGUIMode()), so a temporary QJsonValue::toString() result cannot bind
    // to the parameter directly.
    QString formulaLength = args.value(QStringLiteral("length")).toString();
    QString formulaAngle = args.contains(QStringLiteral("angle")) ? args.value(QStringLiteral("angle")).toString()
                                                                    : QStringLiteral("0"); // DialogEndLine's own angle default.
    const QString lineType = lineTypeOrDefault(args);
    const QString lineWeight = lineWeightOrDefault(args);
    const QString lineColor = lineColorOrDefault(args);

    return runCreate(QStringLiteral("endLine"), [&]() -> ActionResult {
        // _id=0, Document::FullParse, Source::FromGui: the exact same triple point_handlers.cpp's
        // basePoint and line_handlers.cpp's line already use (verified against those files rather
        // than assumed) -- Source::FromGui is what makes Create() call VContainer::getNextId()
        // internally (via AddGObject()) rather than requiring the caller to pre-assign one.
        VToolEndLine *tool = VToolEndLine::Create(0, common.name, lineType, lineWeight, lineColor, formulaLength,
                                                   formulaAngle, basePointId, common.mx, common.my,
                                                   common.showPointName, scene, doc, data, Document::FullParse,
                                                   Source::FromGui);
        if (tool == nullptr) // Create() only returns nullptr for a parse mode other than FullParse; defensive guard, unreachable here.
        {
            return ActionResult::failure(QStringLiteral("endLine: tool creation failed for an unknown reason"));
        }

        QJsonObject payload;
        payload["id"] = static_cast<qint64>(tool->getId());
        payload["name"] = common.name;
        payload["op"] = QStringLiteral("endLine");
        return ActionResult::success(payload);
    });
}

// Implements "alongLine": see formula_point_handlers.h for the documented JSON shape.
ActionResult handleAlongLine(const QJsonObject &args, const ActionContext &ctx)
{
    const CommonPointArgs common = parseCommonPointArgs(args);
    if (common.name.isEmpty())
    {
        return ActionResult::failure(QStringLiteral("alongLine requires a non-empty \"name\""));
    }

    const QString firstName = args.value(QStringLiteral("firstPoint")).toString();
    const QString secondName = args.value(QStringLiteral("secondPoint")).toString();
    if (firstName.isEmpty() || secondName.isEmpty())
    {
        return ActionResult::failure(QStringLiteral("alongLine requires non-empty \"firstPoint\" and \"secondPoint\""));
    }
    if (!args.contains(QStringLiteral("length")))
    {
        return ActionResult::failure(QStringLiteral("alongLine requires a \"length\" formula"));
    }

    VContainer *data = ctx.data();
    VAbstractPattern *doc = ctx.doc();
    VMainGraphicsScene *scene = ctx.scene();
    if (data == nullptr || doc == nullptr || scene == nullptr)
    {
        return ActionResult::failure(QStringLiteral("alongLine: context is missing a scene, document, or data container"));
    }

    const quint32 firstId = NameResolver::idForName(firstName, data, Draw::Calculation);   // Uncaught by design; see handleEndLine()'s comment.
    const quint32 secondId = NameResolver::idForName(secondName, data, Draw::Calculation); // Uncaught by design; see handleEndLine()'s comment.

    const QString firstTypeError = checkIsPoint(data, firstId, QStringLiteral("firstPoint"), firstName);
    if (!firstTypeError.isEmpty())
    {
        return ActionResult::failure(firstTypeError);
    }
    const QString secondTypeError = checkIsPoint(data, secondId, QStringLiteral("secondPoint"), secondName);
    if (!secondTypeError.isEmpty())
    {
        return ActionResult::failure(secondTypeError);
    }

    QString formula = args.value(QStringLiteral("length")).toString(); // VToolAlongLine::Create() takes this by QString&; see handleEndLine()'s comment on why a local copy is needed.
    const QString lineType = lineTypeOrDefault(args);
    const QString lineWeight = lineWeightOrDefault(args);
    const QString lineColor = lineColorOrDefault(args);

    return runCreate(QStringLiteral("alongLine"), [&]() -> ActionResult {
        VToolAlongLine *tool = VToolAlongLine::Create(0, common.name, lineType, lineWeight, lineColor, formula,
                                                        firstId, secondId, common.mx, common.my, common.showPointName,
                                                        scene, doc, data, Document::FullParse, Source::FromGui);
        if (tool == nullptr)
        {
            return ActionResult::failure(QStringLiteral("alongLine: tool creation failed for an unknown reason"));
        }

        QJsonObject payload;
        payload["id"] = static_cast<qint64>(tool->getId());
        payload["name"] = common.name;
        payload["op"] = QStringLiteral("alongLine");
        return ActionResult::success(payload);
    });
}

// Implements "normal": see formula_point_handlers.h for the documented JSON shape. Note "angle"
// here is a plain numeric rotation offset (VToolNormal::Create()'s own "const qreal angle"
// parameter), not a formula -- unlike "length", it is never passed through CheckFormula()/
// qmu::QmuParserError, so it is parsed directly with QJsonValue::toDouble() below.
ActionResult handleNormal(const QJsonObject &args, const ActionContext &ctx)
{
    const CommonPointArgs common = parseCommonPointArgs(args);
    if (common.name.isEmpty())
    {
        return ActionResult::failure(QStringLiteral("normal requires a non-empty \"name\""));
    }

    const QString firstName = args.value(QStringLiteral("firstPoint")).toString();
    const QString secondName = args.value(QStringLiteral("secondPoint")).toString();
    if (firstName.isEmpty() || secondName.isEmpty())
    {
        return ActionResult::failure(QStringLiteral("normal requires non-empty \"firstPoint\" and \"secondPoint\""));
    }
    if (!args.contains(QStringLiteral("length")))
    {
        return ActionResult::failure(QStringLiteral("normal requires a \"length\" formula"));
    }

    VContainer *data = ctx.data();
    VAbstractPattern *doc = ctx.doc();
    VMainGraphicsScene *scene = ctx.scene();
    if (data == nullptr || doc == nullptr || scene == nullptr)
    {
        return ActionResult::failure(QStringLiteral("normal: context is missing a scene, document, or data container"));
    }

    const quint32 firstId = NameResolver::idForName(firstName, data, Draw::Calculation);
    const quint32 secondId = NameResolver::idForName(secondName, data, Draw::Calculation);

    const QString firstTypeError = checkIsPoint(data, firstId, QStringLiteral("firstPoint"), firstName);
    if (!firstTypeError.isEmpty())
    {
        return ActionResult::failure(firstTypeError);
    }
    const QString secondTypeError = checkIsPoint(data, secondId, QStringLiteral("secondPoint"), secondName);
    if (!secondTypeError.isEmpty())
    {
        return ActionResult::failure(secondTypeError);
    }

    QString formula = args.value(QStringLiteral("length")).toString();
    // "angle" is read via toVariant().toDouble() rather than QJsonValue::toDouble() directly, so it
    // accepts either a bare JSON number (like basePoint's "x"/"y") or a JSON string (as every
    // fixture in this test suite writes it, e.g. "0", to stay visually consistent with the formula
    // fields beside it) -- QJsonValue::toDouble() alone only converts a JSON number and would
    // silently fall back to the default for a string value.
    const qreal angle = args.contains(QStringLiteral("angle")) ? args.value(QStringLiteral("angle")).toVariant().toDouble()
                                                                 : 0.0; // VToolNormal::ReadToolAttributes()'s own "0" default.
    const QString lineType = lineTypeOrDefault(args);
    const QString lineWeight = lineWeightOrDefault(args);
    const QString lineColor = lineColorOrDefault(args);

    return runCreate(QStringLiteral("normal"), [&]() -> ActionResult {
        VToolNormal *tool = VToolNormal::Create(0, formula, firstId, secondId, lineType, lineWeight, lineColor,
                                                  common.name, angle, common.mx, common.my, common.showPointName,
                                                  scene, doc, data, Document::FullParse, Source::FromGui);
        if (tool == nullptr)
        {
            return ActionResult::failure(QStringLiteral("normal: tool creation failed for an unknown reason"));
        }

        QJsonObject payload;
        payload["id"] = static_cast<qint64>(tool->getId());
        payload["name"] = common.name;
        payload["op"] = QStringLiteral("normal");
        return ActionResult::success(payload);
    });
}

// Implements "bisector": see formula_point_handlers.h for the documented JSON shape.
ActionResult handleBisector(const QJsonObject &args, const ActionContext &ctx)
{
    const CommonPointArgs common = parseCommonPointArgs(args);
    if (common.name.isEmpty())
    {
        return ActionResult::failure(QStringLiteral("bisector requires a non-empty \"name\""));
    }

    const QString firstName = args.value(QStringLiteral("firstPoint")).toString();
    const QString secondName = args.value(QStringLiteral("secondPoint")).toString();
    const QString thirdName = args.value(QStringLiteral("thirdPoint")).toString();
    if (firstName.isEmpty() || secondName.isEmpty() || thirdName.isEmpty())
    {
        return ActionResult::failure(
            QStringLiteral("bisector requires non-empty \"firstPoint\", \"secondPoint\", and \"thirdPoint\""));
    }
    if (!args.contains(QStringLiteral("length")))
    {
        return ActionResult::failure(QStringLiteral("bisector requires a \"length\" formula"));
    }

    VContainer *data = ctx.data();
    VAbstractPattern *doc = ctx.doc();
    VMainGraphicsScene *scene = ctx.scene();
    if (data == nullptr || doc == nullptr || scene == nullptr)
    {
        return ActionResult::failure(QStringLiteral("bisector: context is missing a scene, document, or data container"));
    }

    const quint32 firstId = NameResolver::idForName(firstName, data, Draw::Calculation);
    const quint32 secondId = NameResolver::idForName(secondName, data, Draw::Calculation);
    const quint32 thirdId = NameResolver::idForName(thirdName, data, Draw::Calculation);

    const QString firstTypeError = checkIsPoint(data, firstId, QStringLiteral("firstPoint"), firstName);
    if (!firstTypeError.isEmpty())
    {
        return ActionResult::failure(firstTypeError);
    }
    const QString secondTypeError = checkIsPoint(data, secondId, QStringLiteral("secondPoint"), secondName);
    if (!secondTypeError.isEmpty())
    {
        return ActionResult::failure(secondTypeError);
    }
    const QString thirdTypeError = checkIsPoint(data, thirdId, QStringLiteral("thirdPoint"), thirdName);
    if (!thirdTypeError.isEmpty())
    {
        return ActionResult::failure(thirdTypeError);
    }

    QString formula = args.value(QStringLiteral("length")).toString();
    const QString lineType = lineTypeOrDefault(args);
    const QString lineWeight = lineWeightOrDefault(args);
    const QString lineColor = lineColorOrDefault(args);

    return runCreate(QStringLiteral("bisector"), [&]() -> ActionResult {
        VToolBisector *tool = VToolBisector::Create(0, formula, firstId, secondId, thirdId, lineType, lineWeight,
                                                      lineColor, common.name, common.mx, common.my,
                                                      common.showPointName, scene, doc, data, Document::FullParse,
                                                      Source::FromGui);
        if (tool == nullptr)
        {
            return ActionResult::failure(QStringLiteral("bisector: tool creation failed for an unknown reason"));
        }

        QJsonObject payload;
        payload["id"] = static_cast<qint64>(tool->getId());
        payload["name"] = common.name;
        payload["op"] = QStringLiteral("bisector");
        return ActionResult::success(payload);
    });
}

// Implements "shoulderPoint": see formula_point_handlers.h for the documented JSON shape.
ActionResult handleShoulderPoint(const QJsonObject &args, const ActionContext &ctx)
{
    const CommonPointArgs common = parseCommonPointArgs(args);
    if (common.name.isEmpty())
    {
        return ActionResult::failure(QStringLiteral("shoulderPoint requires a non-empty \"name\""));
    }

    const QString p1LineName = args.value(QStringLiteral("p1Line")).toString();
    const QString p2LineName = args.value(QStringLiteral("p2Line")).toString();
    const QString pShoulderName = args.value(QStringLiteral("pShoulder")).toString();
    if (p1LineName.isEmpty() || p2LineName.isEmpty() || pShoulderName.isEmpty())
    {
        return ActionResult::failure(
            QStringLiteral("shoulderPoint requires non-empty \"p1Line\", \"p2Line\", and \"pShoulder\""));
    }
    if (!args.contains(QStringLiteral("length")))
    {
        return ActionResult::failure(QStringLiteral("shoulderPoint requires a \"length\" formula"));
    }

    VContainer *data = ctx.data();
    VAbstractPattern *doc = ctx.doc();
    VMainGraphicsScene *scene = ctx.scene();
    if (data == nullptr || doc == nullptr || scene == nullptr)
    {
        return ActionResult::failure(
            QStringLiteral("shoulderPoint: context is missing a scene, document, or data container"));
    }

    const quint32 p1LineId = NameResolver::idForName(p1LineName, data, Draw::Calculation);
    const quint32 p2LineId = NameResolver::idForName(p2LineName, data, Draw::Calculation);
    const quint32 pShoulderId = NameResolver::idForName(pShoulderName, data, Draw::Calculation);

    const QString p1TypeError = checkIsPoint(data, p1LineId, QStringLiteral("p1Line"), p1LineName);
    if (!p1TypeError.isEmpty())
    {
        return ActionResult::failure(p1TypeError);
    }
    const QString p2TypeError = checkIsPoint(data, p2LineId, QStringLiteral("p2Line"), p2LineName);
    if (!p2TypeError.isEmpty())
    {
        return ActionResult::failure(p2TypeError);
    }
    const QString shoulderTypeError = checkIsPoint(data, pShoulderId, QStringLiteral("pShoulder"), pShoulderName);
    if (!shoulderTypeError.isEmpty())
    {
        return ActionResult::failure(shoulderTypeError);
    }

    QString formula = args.value(QStringLiteral("length")).toString();
    const QString lineType = lineTypeOrDefault(args);
    const QString lineWeight = lineWeightOrDefault(args);
    const QString lineColor = lineColorOrDefault(args);

    return runCreate(QStringLiteral("shoulderPoint"), [&]() -> ActionResult {
        VToolShoulderPoint *tool = VToolShoulderPoint::Create(0, formula, p1LineId, p2LineId, pShoulderId, lineType,
                                                                 lineWeight, lineColor, common.name, common.mx,
                                                                 common.my, common.showPointName, scene, doc, data,
                                                                 Document::FullParse, Source::FromGui);
        if (tool == nullptr)
        {
            return ActionResult::failure(QStringLiteral("shoulderPoint: tool creation failed for an unknown reason"));
        }

        QJsonObject payload;
        payload["id"] = static_cast<qint64>(tool->getId());
        payload["name"] = common.name;
        payload["op"] = QStringLiteral("shoulderPoint");
        return ActionResult::success(payload);
    });
}

// Implements "lineIntersect": see formula_point_handlers.h for the documented JSON shape. Unlike
// the five ops above, VToolLineIntersect::Create() involves no formula at all -- so runCreate()'s
// qmu::QmuParserError clause is dead code for this op specifically, but it is still wrapped
// through the same helper for two reasons: uniformity with the rest of this file, and because
// Create() still reaches VException-throwing code (AddToFile()'s undo push) on the success path.
ActionResult handleLineIntersect(const QJsonObject &args, const ActionContext &ctx)
{
    const CommonPointArgs common = parseCommonPointArgs(args);
    if (common.name.isEmpty())
    {
        return ActionResult::failure(QStringLiteral("lineIntersect requires a non-empty \"name\""));
    }

    const QString p1Line1Name = args.value(QStringLiteral("p1Line1")).toString();
    const QString p2Line1Name = args.value(QStringLiteral("p2Line1")).toString();
    const QString p1Line2Name = args.value(QStringLiteral("p1Line2")).toString();
    const QString p2Line2Name = args.value(QStringLiteral("p2Line2")).toString();
    if (p1Line1Name.isEmpty() || p2Line1Name.isEmpty() || p1Line2Name.isEmpty() || p2Line2Name.isEmpty())
    {
        return ActionResult::failure(
            QStringLiteral("lineIntersect requires non-empty \"p1Line1\", \"p2Line1\", \"p1Line2\", and \"p2Line2\""));
    }

    VContainer *data = ctx.data();
    VAbstractPattern *doc = ctx.doc();
    VMainGraphicsScene *scene = ctx.scene();
    if (data == nullptr || doc == nullptr || scene == nullptr)
    {
        return ActionResult::failure(
            QStringLiteral("lineIntersect: context is missing a scene, document, or data container"));
    }

    const quint32 p1Line1Id = NameResolver::idForName(p1Line1Name, data, Draw::Calculation);
    const quint32 p2Line1Id = NameResolver::idForName(p2Line1Name, data, Draw::Calculation);
    const quint32 p1Line2Id = NameResolver::idForName(p1Line2Name, data, Draw::Calculation);
    const quint32 p2Line2Id = NameResolver::idForName(p2Line2Name, data, Draw::Calculation);

    const QString p1Line1TypeError = checkIsPoint(data, p1Line1Id, QStringLiteral("p1Line1"), p1Line1Name);
    if (!p1Line1TypeError.isEmpty())
    {
        return ActionResult::failure(p1Line1TypeError);
    }
    const QString p2Line1TypeError = checkIsPoint(data, p2Line1Id, QStringLiteral("p2Line1"), p2Line1Name);
    if (!p2Line1TypeError.isEmpty())
    {
        return ActionResult::failure(p2Line1TypeError);
    }
    const QString p1Line2TypeError = checkIsPoint(data, p1Line2Id, QStringLiteral("p1Line2"), p1Line2Name);
    if (!p1Line2TypeError.isEmpty())
    {
        return ActionResult::failure(p1Line2TypeError);
    }
    const QString p2Line2TypeError = checkIsPoint(data, p2Line2Id, QStringLiteral("p2Line2"), p2Line2Name);
    if (!p2Line2TypeError.isEmpty())
    {
        return ActionResult::failure(p2Line2TypeError);
    }

    return runCreate(QStringLiteral("lineIntersect"), [&]() -> ActionResult {
        VToolLineIntersect *tool = VToolLineIntersect::Create(0, p1Line1Id, p2Line1Id, p1Line2Id, p2Line2Id,
                                                                 common.name, common.mx, common.my,
                                                                 common.showPointName, scene, doc, data,
                                                                 Document::FullParse, Source::FromGui);
        if (tool == nullptr)
        {
            // Unlike the defensive-only nullptr guards in the five handlers above, this is a real,
            // reachable outcome: VToolLineIntersect::Create() (src/libs/vtools/tools/drawTools/
            // toolpoint/toolsinglepoint/vtoollineintersect.cpp) returns nullptr whenever
            // QLineF::intersects() reports NoIntersection -- i.e. the two named lines are exactly
            // parallel -- since we always pass Document::FullParse (ruling out the *other* nullptr
            // path, "parse != FullParse", which never applies to this handler).
            QJsonObject detail = structuredError(QStringLiteral("noIntersection"),
                QStringLiteral("Lines %1-%2 and %3-%4 do not intersect (parallel)")
                    .arg(p1Line1Name, p2Line1Name, p1Line2Name, p2Line2Name));
            return ActionResult::failure(QJsonValue(detail));
        }

        QJsonObject payload;
        payload["id"] = static_cast<qint64>(tool->getId());
        payload["name"] = common.name;
        payload["op"] = QStringLiteral("lineIntersect");
        return ActionResult::success(payload);
    });
}
