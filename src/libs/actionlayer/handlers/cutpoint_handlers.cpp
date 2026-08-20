//---------------------------------------------------------------------------------------------------------------------
//  @file   cutpoint_handlers.cpp
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

#include "cutpoint_handlers.h"

#include "../action_context.h"
#include "../name_resolver.h"

#include "../../vpatterndb/vcontainer.h"
#include "../../vgeometry/vgobject.h"
#include "../../vgeometry/vgeometrydef.h"
#include "../../ifc/xml/vabstractpattern.h"
#include "../../ifc/exception/vexception.h"
#include "../../ifc/ifcdef.h"
#include "../../qmuparser/qmuparsererror.h"
#include "../../vwidgets/vmaingraphicsscene.h"

#include "../../vtools/tools/drawTools/toolpoint/toolsinglepoint/toolcut/vtoolcutspline.h"
#include "../../vtools/tools/drawTools/toolpoint/toolsinglepoint/toolcut/vtoolcutarc.h"
#include "../../vtools/tools/drawTools/toolpoint/toolsinglepoint/vtoolpointofintersectionarcs.h"
#include "../../vtools/tools/drawTools/toolpoint/toolsinglepoint/intersect_circles_tool.h"
#include "../../vtools/tools/drawTools/toolpoint/toolsinglepoint/vtoolpointofintersectioncurves.h"
#include "../../vtools/tools/drawTools/toolpoint/toolsinglepoint/toollinepoint/vtoolcurveintersectaxis.h"
#include "../../vtools/tools/drawTools/toolpoint/toolsinglepoint/intersect_circletangent_tool.h"
#include "../../vtools/tools/drawTools/toolpoint/toolsinglepoint/vtoolpointfromarcandtangent.h"
#include "../../vtools/tools/drawTools/toolpoint/toolsinglepoint/vtooltriangle.h"
#include "../../vtools/tools/drawTools/toolpoint/toolsinglepoint/toollinepoint/vtoolheight.h"

#include <QJsonObject>
#include <QSharedPointer>
#include <QString>

namespace
{
    QJsonObject structuredError(const QString &type, const QString &message)
    {
        QJsonObject error;
        error["type"] = type;
        error["message"] = message;
        return error;
    }

    QString checkGOType(const VContainer *data, quint32 id, GOType expected, const QString &expectedName,
                        const QString &fieldName, const QString &name)
    {
        const QSharedPointer<VGObject> obj = data->GetGObject(id);
        if (obj.isNull() || obj->getType() != expected)
        {
            return QStringLiteral("\"%1\" (\"%2\") does not name a %3").arg(fieldName, name, expectedName);
        }
        return QString();
    }

    bool isCurveType(GOType type)
    {
        return type == GOType::Arc || type == GOType::EllipticalArc || type == GOType::Spline
            || type == GOType::SplinePath || type == GOType::CubicBezier || type == GOType::CubicBezierPath;
    }

    // "curve" here accepts any VAbstractCubicBezier-derived curve (spline/splinePath/cubicBezier/
    // cubicBezierPath), matching VToolCutSpline::Create()'s own data->GeometricObject<VAbstractCubicBezier>()
    // cast -- not literally GOType::Spline only.
    QString checkIsCubicBezierCurve(const VContainer *data, quint32 id, const QString &fieldName, const QString &name)
    {
        const QSharedPointer<VGObject> obj = data->GetGObject(id);
        if (obj.isNull()
            || (obj->getType() != GOType::Spline && obj->getType() != GOType::SplinePath
                && obj->getType() != GOType::CubicBezier && obj->getType() != GOType::CubicBezierPath))
        {
            return QStringLiteral("\"%1\" (\"%2\") does not name a spline/splinePath/cubicBezier/cubicBezierPath curve")
                .arg(fieldName, name);
        }
        return QString();
    }

    // "curve" here accepts any curve type at all (used by curveIntersectAxis/pointOfIntersectionCurves,
    // which both operate on the generic VAbstractCurve base -- including arcs).
    QString checkIsAnyCurve(const VContainer *data, quint32 id, const QString &fieldName, const QString &name)
    {
        const QSharedPointer<VGObject> obj = data->GetGObject(id);
        if (obj.isNull() || !isCurveType(obj->getType()))
        {
            return QStringLiteral("\"%1\" (\"%2\") does not name a curve").arg(fieldName, name);
        }
        return QString();
    }

    QString checkIsPoint(const VContainer *data, quint32 id, const QString &fieldName, const QString &name)
    {
        return checkGOType(data, id, GOType::Point, QStringLiteral("point"), fieldName, name);
    }

    QString checkIsArc(const VContainer *data, quint32 id, const QString &fieldName, const QString &name)
    {
        return checkGOType(data, id, GOType::Arc, QStringLiteral("arc"), fieldName, name);
    }

    template <class CreateFn>
    ActionResult runCreate(const QString &op, CreateFn create)
    {
        try
        {
            return create();
        }
        catch (const qmu::QmuParserError &error)
        {
            QJsonObject detail = structuredError(QStringLiteral("formulaError"),
                QStringLiteral("Formula error: %1").arg(error.GetMsg()));
            detail["op"] = op;
            detail["message"] = error.GetMsg();
            detail["expr"] = error.GetExpr();
            return ActionResult::failure(QJsonValue(detail));
        }
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
            return ActionResult::failure(QStringLiteral("%1: unknown error creating the point").arg(op));
        }
    }

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

    // "forward" or anything else (treated as backward), exactly matching VToolCutSpline::Create()'s
    // and VToolCutArc::Create()'s own `direction == "forward"` check -- there is no third value.
    bool directionRequiresForward(const QJsonObject &args, const QString &op, QString &outError)
    {
        if (!args.contains(QStringLiteral("direction")))
        {
            outError = QStringLiteral("%1 requires a \"direction\" (\"forward\" or \"backward\")").arg(op);
            return false;
        }
        return true;
    }

    // Parses a CrossCirclesPoint from "firstPoint"/"secondPoint" (case-insensitive); returns false
    // (and fills outError) for anything else instead of silently defaulting.
    bool parseCrossCirclesPoint(const QJsonObject &args, const QString &op, CrossCirclesPoint &outValue, QString &outError)
    {
        const QString raw = args.value(QStringLiteral("crossPoint")).toString();
        if (raw.compare(QStringLiteral("firstPoint"), Qt::CaseInsensitive) == 0)
        {
            outValue = CrossCirclesPoint::FirstPoint;
            return true;
        }
        if (raw.compare(QStringLiteral("secondPoint"), Qt::CaseInsensitive) == 0)
        {
            outValue = CrossCirclesPoint::SecondPoint;
            return true;
        }
        outError = QStringLiteral("%1 requires \"crossPoint\" to be \"firstPoint\" or \"secondPoint\" (got \"%2\")")
                       .arg(op, raw);
        return false;
    }

    bool parseVCrossCurvesPoint(const QJsonObject &args, const QString &op, VCrossCurvesPoint &outValue, QString &outError)
    {
        const QString raw = args.value(QStringLiteral("vCrossPoint")).toString();
        if (raw.compare(QStringLiteral("highestPoint"), Qt::CaseInsensitive) == 0)
        {
            outValue = VCrossCurvesPoint::HighestPoint;
            return true;
        }
        if (raw.compare(QStringLiteral("lowestPoint"), Qt::CaseInsensitive) == 0)
        {
            outValue = VCrossCurvesPoint::LowestPoint;
            return true;
        }
        outError = QStringLiteral("%1 requires \"vCrossPoint\" to be \"highestPoint\" or \"lowestPoint\" (got \"%2\")")
                       .arg(op, raw);
        return false;
    }

    bool parseHCrossCurvesPoint(const QJsonObject &args, const QString &op, HCrossCurvesPoint &outValue, QString &outError)
    {
        const QString raw = args.value(QStringLiteral("hCrossPoint")).toString();
        if (raw.compare(QStringLiteral("leftmostPoint"), Qt::CaseInsensitive) == 0)
        {
            outValue = HCrossCurvesPoint::LeftmostPoint;
            return true;
        }
        if (raw.compare(QStringLiteral("rightmostPoint"), Qt::CaseInsensitive) == 0)
        {
            outValue = HCrossCurvesPoint::RightmostPoint;
            return true;
        }
        outError = QStringLiteral("%1 requires \"hCrossPoint\" to be \"leftmostPoint\" or \"rightmostPoint\" (got \"%2\")")
                       .arg(op, raw);
        return false;
    }
}

// Implements "cutSpline": see cutpoint_handlers.h for the documented JSON shape.
ActionResult handleCutSpline(const QJsonObject &args, const ActionContext &ctx)
{
    const CommonPointArgs common = parseCommonPointArgs(args);
    if (common.name.isEmpty())
    {
        return ActionResult::failure(QStringLiteral("cutSpline requires a non-empty \"name\""));
    }
    const QString curveName = args.value(QStringLiteral("curve")).toString();
    if (curveName.isEmpty())
    {
        return ActionResult::failure(QStringLiteral("cutSpline requires a non-empty \"curve\""));
    }
    if (!args.contains(QStringLiteral("length")))
    {
        return ActionResult::failure(QStringLiteral("cutSpline requires a \"length\" formula"));
    }
    QString directionError;
    if (!directionRequiresForward(args, QStringLiteral("cutSpline"), directionError))
    {
        return ActionResult::failure(directionError);
    }

    VContainer *data = ctx.data();
    VAbstractPattern *doc = ctx.doc();
    VMainGraphicsScene *scene = ctx.scene();
    if (data == nullptr || doc == nullptr || scene == nullptr)
    {
        return ActionResult::failure(QStringLiteral("cutSpline: context is missing a scene, document, or data container"));
    }

    const quint32 curveId = NameResolver::idForName(curveName, data);
    const QString typeError = checkIsCubicBezierCurve(data, curveId, QStringLiteral("curve"), curveName);
    if (!typeError.isEmpty())
    {
        return ActionResult::failure(typeError);
    }

    QString direction = args.value(QStringLiteral("direction")).toString();
    QString formula = args.value(QStringLiteral("length")).toString();
    const QString lineColor = lineColorOrDefault(args);

    return runCreate(QStringLiteral("cutSpline"), [&]() -> ActionResult {
        VToolCutSpline *tool = VToolCutSpline::Create(0, common.name, direction, formula, lineColor, curveId,
                                                        common.mx, common.my, common.showPointName, scene, doc, data,
                                                        Document::FullParse, Source::FromGui);
        if (tool == nullptr)
        {
            return ActionResult::failure(QStringLiteral("cutSpline: tool creation failed for an unknown reason"));
        }

        QJsonObject payload;
        payload["id"] = static_cast<qint64>(tool->getId());
        payload["name"] = common.name;
        payload["op"] = QStringLiteral("cutSpline");
        return ActionResult::success(payload);
    });
}

// Implements "cutArc": see cutpoint_handlers.h for the documented JSON shape.
ActionResult handleCutArc(const QJsonObject &args, const ActionContext &ctx)
{
    const CommonPointArgs common = parseCommonPointArgs(args);
    if (common.name.isEmpty())
    {
        return ActionResult::failure(QStringLiteral("cutArc requires a non-empty \"name\""));
    }
    const QString arcName = args.value(QStringLiteral("arc")).toString();
    if (arcName.isEmpty())
    {
        return ActionResult::failure(QStringLiteral("cutArc requires a non-empty \"arc\""));
    }
    if (!args.contains(QStringLiteral("length")))
    {
        return ActionResult::failure(QStringLiteral("cutArc requires a \"length\" formula"));
    }
    QString directionError;
    if (!directionRequiresForward(args, QStringLiteral("cutArc"), directionError))
    {
        return ActionResult::failure(directionError);
    }

    VContainer *data = ctx.data();
    VAbstractPattern *doc = ctx.doc();
    VMainGraphicsScene *scene = ctx.scene();
    if (data == nullptr || doc == nullptr || scene == nullptr)
    {
        return ActionResult::failure(QStringLiteral("cutArc: context is missing a scene, document, or data container"));
    }

    const quint32 arcId = NameResolver::idForName(arcName, data);
    const QString typeError = checkIsArc(data, arcId, QStringLiteral("arc"), arcName);
    if (!typeError.isEmpty())
    {
        return ActionResult::failure(typeError);
    }

    QString direction = args.value(QStringLiteral("direction")).toString();
    QString formula = args.value(QStringLiteral("length")).toString();
    const QString lineColor = lineColorOrDefault(args);

    return runCreate(QStringLiteral("cutArc"), [&]() -> ActionResult {
        VToolCutArc *tool = VToolCutArc::Create(0, common.name, direction, formula, lineColor, arcId, common.mx,
                                                  common.my, common.showPointName, scene, doc, data,
                                                  Document::FullParse, Source::FromGui);
        if (tool == nullptr)
        {
            return ActionResult::failure(QStringLiteral("cutArc: tool creation failed for an unknown reason"));
        }

        QJsonObject payload;
        payload["id"] = static_cast<qint64>(tool->getId());
        payload["name"] = common.name;
        payload["op"] = QStringLiteral("cutArc");
        return ActionResult::success(payload);
    });
}

// Implements "pointOfIntersectionArcs": see cutpoint_handlers.h for the documented JSON shape.
ActionResult handlePointOfIntersectionArcs(const QJsonObject &args, const ActionContext &ctx)
{
    const CommonPointArgs common = parseCommonPointArgs(args);
    if (common.name.isEmpty())
    {
        return ActionResult::failure(QStringLiteral("pointOfIntersectionArcs requires a non-empty \"name\""));
    }
    const QString firstArcName = args.value(QStringLiteral("firstArc")).toString();
    const QString secondArcName = args.value(QStringLiteral("secondArc")).toString();
    if (firstArcName.isEmpty() || secondArcName.isEmpty())
    {
        return ActionResult::failure(
            QStringLiteral("pointOfIntersectionArcs requires non-empty \"firstArc\" and \"secondArc\""));
    }

    VContainer *data = ctx.data();
    VAbstractPattern *doc = ctx.doc();
    VMainGraphicsScene *scene = ctx.scene();
    if (data == nullptr || doc == nullptr || scene == nullptr)
    {
        return ActionResult::failure(
            QStringLiteral("pointOfIntersectionArcs: context is missing a scene, document, or data container"));
    }

    const quint32 firstArcId = NameResolver::idForName(firstArcName, data);
    const quint32 secondArcId = NameResolver::idForName(secondArcName, data);
    const QString firstTypeError = checkIsArc(data, firstArcId, QStringLiteral("firstArc"), firstArcName);
    if (!firstTypeError.isEmpty())
    {
        return ActionResult::failure(firstTypeError);
    }
    const QString secondTypeError = checkIsArc(data, secondArcId, QStringLiteral("secondArc"), secondArcName);
    if (!secondTypeError.isEmpty())
    {
        return ActionResult::failure(secondTypeError);
    }

    CrossCirclesPoint crossPoint;
    QString crossPointError;
    if (!parseCrossCirclesPoint(args, QStringLiteral("pointOfIntersectionArcs"), crossPoint, crossPointError))
    {
        return ActionResult::failure(crossPointError);
    }

    return runCreate(QStringLiteral("pointOfIntersectionArcs"), [&]() -> ActionResult {
        VToolPointOfIntersectionArcs *tool = VToolPointOfIntersectionArcs::Create(
            0, common.name, firstArcId, secondArcId, crossPoint, common.mx, common.my, scene, doc, data,
            Document::FullParse, Source::FromGui);
        if (tool == nullptr)
        {
            return ActionResult::failure(
                QStringLiteral("pointOfIntersectionArcs: tool creation failed for an unknown reason"));
        }

        QJsonObject payload;
        payload["id"] = static_cast<qint64>(tool->getId());
        payload["name"] = common.name;
        payload["op"] = QStringLiteral("pointOfIntersectionArcs");
        return ActionResult::success(payload);
    });
}

// Implements "pointOfIntersectionCircles": see cutpoint_handlers.h for the documented JSON shape.
ActionResult handlePointOfIntersectionCircles(const QJsonObject &args, const ActionContext &ctx)
{
    const CommonPointArgs common = parseCommonPointArgs(args);
    if (common.name.isEmpty())
    {
        return ActionResult::failure(QStringLiteral("pointOfIntersectionCircles requires a non-empty \"name\""));
    }
    const QString firstCenterName = args.value(QStringLiteral("firstCircleCenter")).toString();
    const QString secondCenterName = args.value(QStringLiteral("secondCircleCenter")).toString();
    if (firstCenterName.isEmpty() || secondCenterName.isEmpty())
    {
        return ActionResult::failure(QStringLiteral(
            "pointOfIntersectionCircles requires non-empty \"firstCircleCenter\" and \"secondCircleCenter\""));
    }
    if (!args.contains(QStringLiteral("firstCircleRadius")) || !args.contains(QStringLiteral("secondCircleRadius")))
    {
        return ActionResult::failure(QStringLiteral(
            "pointOfIntersectionCircles requires \"firstCircleRadius\" and \"secondCircleRadius\" formulas"));
    }

    VContainer *data = ctx.data();
    VAbstractPattern *doc = ctx.doc();
    VMainGraphicsScene *scene = ctx.scene();
    if (data == nullptr || doc == nullptr || scene == nullptr)
    {
        return ActionResult::failure(
            QStringLiteral("pointOfIntersectionCircles: context is missing a scene, document, or data container"));
    }

    const quint32 firstCenterId = NameResolver::idForName(firstCenterName, data);
    const quint32 secondCenterId = NameResolver::idForName(secondCenterName, data);
    const QString firstTypeError = checkIsPoint(data, firstCenterId, QStringLiteral("firstCircleCenter"), firstCenterName);
    if (!firstTypeError.isEmpty())
    {
        return ActionResult::failure(firstTypeError);
    }
    const QString secondTypeError = checkIsPoint(data, secondCenterId, QStringLiteral("secondCircleCenter"), secondCenterName);
    if (!secondTypeError.isEmpty())
    {
        return ActionResult::failure(secondTypeError);
    }

    CrossCirclesPoint crossPoint;
    QString crossPointError;
    if (!parseCrossCirclesPoint(args, QStringLiteral("pointOfIntersectionCircles"), crossPoint, crossPointError))
    {
        return ActionResult::failure(crossPointError);
    }

    QString firstRadius = args.value(QStringLiteral("firstCircleRadius")).toString();
    QString secondRadius = args.value(QStringLiteral("secondCircleRadius")).toString();

    return runCreate(QStringLiteral("pointOfIntersectionCircles"), [&]() -> ActionResult {
        IntersectCirclesTool *tool = IntersectCirclesTool::Create(
            0, common.name, firstCenterId, secondCenterId, firstRadius, secondRadius, crossPoint, common.mx,
            common.my, common.showPointName, scene, doc, data, Document::FullParse, Source::FromGui);
        if (tool == nullptr)
        {
            return ActionResult::failure(
                QStringLiteral("pointOfIntersectionCircles: tool creation failed for an unknown reason"));
        }

        QJsonObject payload;
        payload["id"] = static_cast<qint64>(tool->getId());
        payload["name"] = common.name;
        payload["op"] = QStringLiteral("pointOfIntersectionCircles");
        return ActionResult::success(payload);
    });
}

// Implements "pointOfIntersectionCurves": see cutpoint_handlers.h for the documented JSON shape.
ActionResult handlePointOfIntersectionCurves(const QJsonObject &args, const ActionContext &ctx)
{
    const CommonPointArgs common = parseCommonPointArgs(args);
    if (common.name.isEmpty())
    {
        return ActionResult::failure(QStringLiteral("pointOfIntersectionCurves requires a non-empty \"name\""));
    }
    const QString firstCurveName = args.value(QStringLiteral("firstCurve")).toString();
    const QString secondCurveName = args.value(QStringLiteral("secondCurve")).toString();
    if (firstCurveName.isEmpty() || secondCurveName.isEmpty())
    {
        return ActionResult::failure(
            QStringLiteral("pointOfIntersectionCurves requires non-empty \"firstCurve\" and \"secondCurve\""));
    }

    VContainer *data = ctx.data();
    VAbstractPattern *doc = ctx.doc();
    VMainGraphicsScene *scene = ctx.scene();
    if (data == nullptr || doc == nullptr || scene == nullptr)
    {
        return ActionResult::failure(
            QStringLiteral("pointOfIntersectionCurves: context is missing a scene, document, or data container"));
    }

    const quint32 firstCurveId = NameResolver::idForName(firstCurveName, data);
    const quint32 secondCurveId = NameResolver::idForName(secondCurveName, data);
    const QString firstTypeError = checkIsAnyCurve(data, firstCurveId, QStringLiteral("firstCurve"), firstCurveName);
    if (!firstTypeError.isEmpty())
    {
        return ActionResult::failure(firstTypeError);
    }
    const QString secondTypeError = checkIsAnyCurve(data, secondCurveId, QStringLiteral("secondCurve"), secondCurveName);
    if (!secondTypeError.isEmpty())
    {
        return ActionResult::failure(secondTypeError);
    }

    VCrossCurvesPoint vCrossPoint;
    QString vCrossError;
    if (!parseVCrossCurvesPoint(args, QStringLiteral("pointOfIntersectionCurves"), vCrossPoint, vCrossError))
    {
        return ActionResult::failure(vCrossError);
    }
    HCrossCurvesPoint hCrossPoint;
    QString hCrossError;
    if (!parseHCrossCurvesPoint(args, QStringLiteral("pointOfIntersectionCurves"), hCrossPoint, hCrossError))
    {
        return ActionResult::failure(hCrossError);
    }

    return runCreate(QStringLiteral("pointOfIntersectionCurves"), [&]() -> ActionResult {
        VToolPointOfIntersectionCurves *tool = VToolPointOfIntersectionCurves::Create(
            0, common.name, firstCurveId, secondCurveId, vCrossPoint, hCrossPoint, common.mx, common.my,
            common.showPointName, scene, doc, data, Document::FullParse, Source::FromGui);
        if (tool == nullptr)
        {
            return ActionResult::failure(
                QStringLiteral("pointOfIntersectionCurves: tool creation failed for an unknown reason"));
        }

        QJsonObject payload;
        payload["id"] = static_cast<qint64>(tool->getId());
        payload["name"] = common.name;
        payload["op"] = QStringLiteral("pointOfIntersectionCurves");
        return ActionResult::success(payload);
    });
}

// Implements "curveIntersectAxis": see cutpoint_handlers.h for the documented JSON shape.
ActionResult handleCurveIntersectAxis(const QJsonObject &args, const ActionContext &ctx)
{
    const CommonPointArgs common = parseCommonPointArgs(args);
    if (common.name.isEmpty())
    {
        return ActionResult::failure(QStringLiteral("curveIntersectAxis requires a non-empty \"name\""));
    }
    const QString basePointName = args.value(QStringLiteral("basePoint")).toString();
    const QString curveName = args.value(QStringLiteral("curve")).toString();
    if (basePointName.isEmpty() || curveName.isEmpty())
    {
        return ActionResult::failure(
            QStringLiteral("curveIntersectAxis requires non-empty \"basePoint\" and \"curve\""));
    }
    if (!args.contains(QStringLiteral("angle")))
    {
        return ActionResult::failure(QStringLiteral("curveIntersectAxis requires an \"angle\" formula"));
    }

    VContainer *data = ctx.data();
    VAbstractPattern *doc = ctx.doc();
    VMainGraphicsScene *scene = ctx.scene();
    if (data == nullptr || doc == nullptr || scene == nullptr)
    {
        return ActionResult::failure(
            QStringLiteral("curveIntersectAxis: context is missing a scene, document, or data container"));
    }

    const quint32 basePointId = NameResolver::idForName(basePointName, data);
    const quint32 curveId = NameResolver::idForName(curveName, data);
    const QString baseTypeError = checkIsPoint(data, basePointId, QStringLiteral("basePoint"), basePointName);
    if (!baseTypeError.isEmpty())
    {
        return ActionResult::failure(baseTypeError);
    }
    const QString curveTypeError = checkIsAnyCurve(data, curveId, QStringLiteral("curve"), curveName);
    if (!curveTypeError.isEmpty())
    {
        return ActionResult::failure(curveTypeError);
    }

    QString formulaAngle = args.value(QStringLiteral("angle")).toString();
    const QString lineType = lineTypeOrDefault(args);
    const QString lineWeight = lineWeightOrDefault(args);
    const QString lineColor = lineColorOrDefault(args);

    return runCreate(QStringLiteral("curveIntersectAxis"), [&]() -> ActionResult {
        VToolCurveIntersectAxis *tool = VToolCurveIntersectAxis::Create(
            0, common.name, lineType, lineWeight, lineColor, formulaAngle, basePointId, curveId, common.mx,
            common.my, common.showPointName, scene, doc, data, Document::FullParse, Source::FromGui);
        if (tool == nullptr)
        {
            return ActionResult::failure(
                QStringLiteral("curveIntersectAxis: tool creation failed for an unknown reason"));
        }

        QJsonObject payload;
        payload["id"] = static_cast<qint64>(tool->getId());
        payload["name"] = common.name;
        payload["op"] = QStringLiteral("curveIntersectAxis");
        return ActionResult::success(payload);
    });
}

// Implements "pointFromCircleAndTangent": see cutpoint_handlers.h for the documented JSON shape.
ActionResult handlePointFromCircleAndTangent(const QJsonObject &args, const ActionContext &ctx)
{
    const CommonPointArgs common = parseCommonPointArgs(args);
    if (common.name.isEmpty())
    {
        return ActionResult::failure(QStringLiteral("pointFromCircleAndTangent requires a non-empty \"name\""));
    }
    const QString circleCenterName = args.value(QStringLiteral("circleCenter")).toString();
    const QString tangentPointName = args.value(QStringLiteral("tangentPoint")).toString();
    if (circleCenterName.isEmpty() || tangentPointName.isEmpty())
    {
        return ActionResult::failure(QStringLiteral(
            "pointFromCircleAndTangent requires non-empty \"circleCenter\" and \"tangentPoint\""));
    }
    if (!args.contains(QStringLiteral("circleRadius")))
    {
        return ActionResult::failure(QStringLiteral("pointFromCircleAndTangent requires a \"circleRadius\" formula"));
    }

    VContainer *data = ctx.data();
    VAbstractPattern *doc = ctx.doc();
    VMainGraphicsScene *scene = ctx.scene();
    if (data == nullptr || doc == nullptr || scene == nullptr)
    {
        return ActionResult::failure(
            QStringLiteral("pointFromCircleAndTangent: context is missing a scene, document, or data container"));
    }

    const quint32 circleCenterId = NameResolver::idForName(circleCenterName, data);
    const quint32 tangentPointId = NameResolver::idForName(tangentPointName, data);
    const QString centerTypeError = checkIsPoint(data, circleCenterId, QStringLiteral("circleCenter"), circleCenterName);
    if (!centerTypeError.isEmpty())
    {
        return ActionResult::failure(centerTypeError);
    }
    const QString tangentTypeError = checkIsPoint(data, tangentPointId, QStringLiteral("tangentPoint"), tangentPointName);
    if (!tangentTypeError.isEmpty())
    {
        return ActionResult::failure(tangentTypeError);
    }

    CrossCirclesPoint crossPoint;
    QString crossPointError;
    if (!parseCrossCirclesPoint(args, QStringLiteral("pointFromCircleAndTangent"), crossPoint, crossPointError))
    {
        return ActionResult::failure(crossPointError);
    }

    QString circleRadius = args.value(QStringLiteral("circleRadius")).toString();

    return runCreate(QStringLiteral("pointFromCircleAndTangent"), [&]() -> ActionResult {
        IntersectCircleTangentTool *tool = IntersectCircleTangentTool::Create(
            0, common.name, circleCenterId, circleRadius, tangentPointId, crossPoint, common.mx, common.my,
            common.showPointName, scene, doc, data, Document::FullParse, Source::FromGui);
        if (tool == nullptr)
        {
            return ActionResult::failure(
                QStringLiteral("pointFromCircleAndTangent: tool creation failed for an unknown reason"));
        }

        QJsonObject payload;
        payload["id"] = static_cast<qint64>(tool->getId());
        payload["name"] = common.name;
        payload["op"] = QStringLiteral("pointFromCircleAndTangent");
        return ActionResult::success(payload);
    });
}

// Implements "pointFromArcAndTangent": see cutpoint_handlers.h for the documented JSON shape.
ActionResult handlePointFromArcAndTangent(const QJsonObject &args, const ActionContext &ctx)
{
    const CommonPointArgs common = parseCommonPointArgs(args);
    if (common.name.isEmpty())
    {
        return ActionResult::failure(QStringLiteral("pointFromArcAndTangent requires a non-empty \"name\""));
    }
    const QString arcName = args.value(QStringLiteral("arc")).toString();
    const QString tangentPointName = args.value(QStringLiteral("tangentPoint")).toString();
    if (arcName.isEmpty() || tangentPointName.isEmpty())
    {
        return ActionResult::failure(
            QStringLiteral("pointFromArcAndTangent requires non-empty \"arc\" and \"tangentPoint\""));
    }

    VContainer *data = ctx.data();
    VAbstractPattern *doc = ctx.doc();
    VMainGraphicsScene *scene = ctx.scene();
    if (data == nullptr || doc == nullptr || scene == nullptr)
    {
        return ActionResult::failure(
            QStringLiteral("pointFromArcAndTangent: context is missing a scene, document, or data container"));
    }

    const quint32 arcId = NameResolver::idForName(arcName, data);
    const quint32 tangentPointId = NameResolver::idForName(tangentPointName, data);
    const QString arcTypeError = checkIsArc(data, arcId, QStringLiteral("arc"), arcName);
    if (!arcTypeError.isEmpty())
    {
        return ActionResult::failure(arcTypeError);
    }
    const QString tangentTypeError = checkIsPoint(data, tangentPointId, QStringLiteral("tangentPoint"), tangentPointName);
    if (!tangentTypeError.isEmpty())
    {
        return ActionResult::failure(tangentTypeError);
    }

    CrossCirclesPoint crossPoint;
    QString crossPointError;
    if (!parseCrossCirclesPoint(args, QStringLiteral("pointFromArcAndTangent"), crossPoint, crossPointError))
    {
        return ActionResult::failure(crossPointError);
    }

    return runCreate(QStringLiteral("pointFromArcAndTangent"), [&]() -> ActionResult {
        VToolPointFromArcAndTangent *tool = VToolPointFromArcAndTangent::Create(
            0, common.name, arcId, tangentPointId, crossPoint, common.mx, common.my, common.showPointName, scene,
            doc, data, Document::FullParse, Source::FromGui);
        if (tool == nullptr)
        {
            return ActionResult::failure(
                QStringLiteral("pointFromArcAndTangent: tool creation failed for an unknown reason"));
        }

        QJsonObject payload;
        payload["id"] = static_cast<qint64>(tool->getId());
        payload["name"] = common.name;
        payload["op"] = QStringLiteral("pointFromArcAndTangent");
        return ActionResult::success(payload);
    });
}

// Implements "triangle": see cutpoint_handlers.h for the documented JSON shape.
ActionResult handleTriangle(const QJsonObject &args, const ActionContext &ctx)
{
    const CommonPointArgs common = parseCommonPointArgs(args);
    if (common.name.isEmpty())
    {
        return ActionResult::failure(QStringLiteral("triangle requires a non-empty \"name\""));
    }
    const QString axisP1Name = args.value(QStringLiteral("axisP1")).toString();
    const QString axisP2Name = args.value(QStringLiteral("axisP2")).toString();
    const QString firstPointName = args.value(QStringLiteral("firstPoint")).toString();
    const QString secondPointName = args.value(QStringLiteral("secondPoint")).toString();
    if (axisP1Name.isEmpty() || axisP2Name.isEmpty() || firstPointName.isEmpty() || secondPointName.isEmpty())
    {
        return ActionResult::failure(QStringLiteral(
            "triangle requires non-empty \"axisP1\", \"axisP2\", \"firstPoint\", and \"secondPoint\""));
    }

    VContainer *data = ctx.data();
    VAbstractPattern *doc = ctx.doc();
    VMainGraphicsScene *scene = ctx.scene();
    if (data == nullptr || doc == nullptr || scene == nullptr)
    {
        return ActionResult::failure(QStringLiteral("triangle: context is missing a scene, document, or data container"));
    }

    const quint32 axisP1Id = NameResolver::idForName(axisP1Name, data);
    const quint32 axisP2Id = NameResolver::idForName(axisP2Name, data);
    const quint32 firstPointId = NameResolver::idForName(firstPointName, data);
    const quint32 secondPointId = NameResolver::idForName(secondPointName, data);

    for (const auto &pair : { std::make_pair(axisP1Id, axisP1Name), std::make_pair(axisP2Id, axisP2Name),
                              std::make_pair(firstPointId, firstPointName), std::make_pair(secondPointId, secondPointName) })
    {
        const QString typeError = checkIsPoint(data, pair.first, QStringLiteral("points"), pair.second);
        if (!typeError.isEmpty())
        {
            return ActionResult::failure(typeError);
        }
    }

    return runCreate(QStringLiteral("triangle"), [&]() -> ActionResult {
        VToolTriangle *tool = VToolTriangle::Create(0, common.name, axisP1Id, axisP2Id, firstPointId, secondPointId,
                                                      common.mx, common.my, common.showPointName, scene, doc, data,
                                                      Document::FullParse, Source::FromGui);
        if (tool == nullptr)
        {
            return ActionResult::failure(QStringLiteral("triangle: tool creation failed for an unknown reason"));
        }

        QJsonObject payload;
        payload["id"] = static_cast<qint64>(tool->getId());
        payload["name"] = common.name;
        payload["op"] = QStringLiteral("triangle");
        return ActionResult::success(payload);
    });
}

// Implements "height": see cutpoint_handlers.h for the documented JSON shape.
ActionResult handleHeight(const QJsonObject &args, const ActionContext &ctx)
{
    const CommonPointArgs common = parseCommonPointArgs(args);
    if (common.name.isEmpty())
    {
        return ActionResult::failure(QStringLiteral("height requires a non-empty \"name\""));
    }
    const QString basePointName = args.value(QStringLiteral("basePoint")).toString();
    const QString p1LineName = args.value(QStringLiteral("p1Line")).toString();
    const QString p2LineName = args.value(QStringLiteral("p2Line")).toString();
    if (basePointName.isEmpty() || p1LineName.isEmpty() || p2LineName.isEmpty())
    {
        return ActionResult::failure(
            QStringLiteral("height requires non-empty \"basePoint\", \"p1Line\", and \"p2Line\""));
    }

    VContainer *data = ctx.data();
    VAbstractPattern *doc = ctx.doc();
    VMainGraphicsScene *scene = ctx.scene();
    if (data == nullptr || doc == nullptr || scene == nullptr)
    {
        return ActionResult::failure(QStringLiteral("height: context is missing a scene, document, or data container"));
    }

    const quint32 basePointId = NameResolver::idForName(basePointName, data);
    const quint32 p1LineId = NameResolver::idForName(p1LineName, data);
    const quint32 p2LineId = NameResolver::idForName(p2LineName, data);

    for (const auto &pair : { std::make_pair(basePointId, basePointName), std::make_pair(p1LineId, p1LineName),
                              std::make_pair(p2LineId, p2LineName) })
    {
        const QString typeError = checkIsPoint(data, pair.first, QStringLiteral("points"), pair.second);
        if (!typeError.isEmpty())
        {
            return ActionResult::failure(typeError);
        }
    }

    const QString lineType = lineTypeOrDefault(args);
    const QString lineWeight = lineWeightOrDefault(args);
    const QString lineColor = lineColorOrDefault(args);

    return runCreate(QStringLiteral("height"), [&]() -> ActionResult {
        VToolHeight *tool = VToolHeight::Create(0, common.name, lineType, lineWeight, lineColor, basePointId,
                                                  p1LineId, p2LineId, common.mx, common.my, common.showPointName,
                                                  scene, doc, data, Document::FullParse, Source::FromGui);
        if (tool == nullptr)
        {
            return ActionResult::failure(QStringLiteral("height: tool creation failed for an unknown reason"));
        }

        QJsonObject payload;
        payload["id"] = static_cast<qint64>(tool->getId());
        payload["name"] = common.name;
        payload["op"] = QStringLiteral("height");
        return ActionResult::success(payload);
    });
}
