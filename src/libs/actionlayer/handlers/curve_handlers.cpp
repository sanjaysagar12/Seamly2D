//---------------------------------------------------------------------------------------------------------------------
//  @file   curve_handlers.cpp
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

#include "curve_handlers.h"

#include "../action_context.h"
#include "../name_resolver.h"

#include "../../vpatterndb/vcontainer.h"
#include "../../vgeometry/vgobject.h"
#include "../../vgeometry/vgeometrydef.h"
#include "../../vgeometry/vpointf.h"
#include "../../vgeometry/vcubicbezier.h"
#include "../../vgeometry/vcubicbezierpath.h"
#include "../../ifc/xml/vabstractpattern.h"
#include "../../ifc/exception/vexception.h"
#include "../../ifc/ifcdef.h"
#include "../../qmuparser/qmuparsererror.h"
#include "../../vwidgets/vmaingraphicsscene.h"

#include "../../vtools/tools/drawTools/toolcurve/vtoolspline.h"
#include "../../vtools/tools/drawTools/toolcurve/vtoolsplinepath.h"
#include "../../vtools/tools/drawTools/toolcurve/vtoolcubicbezier.h"
#include "../../vtools/tools/drawTools/toolcurve/vtoolcubicbezierpath.h"
#include "../../vtools/tools/drawTools/toolcurve/vtoolarc.h"
#include "../../vtools/tools/drawTools/toolcurve/vtoolarcwithlength.h"
#include "../../vtools/tools/drawTools/toolcurve/vtoolellipticalarc.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSharedPointer>
#include <QString>
#include <QVector>

namespace
{
    QJsonObject structuredError(const QString &type, const QString &message)
    {
        QJsonObject error;
        error["type"] = type;
        error["message"] = message;
        return error;
    }

    // Same guard as point_handlers.cpp/line_handlers.cpp/formula_point_handlers.cpp: every
    // Create() below internally reaches for a same-id-wrong-type object only via an SCASSERT,
    // compiled to nothing in this project's release build.
    QString checkIsPoint(const VContainer *data, quint32 id, const QString &fieldName, const QString &name)
    {
        const QSharedPointer<VGObject> obj = data->GetGObject(id);
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
        return QString();
    }

    // Runs `create` and converts every failure mode it can produce into an ActionResult, exactly
    // mirroring formula_point_handlers.cpp's runCreate() (see that file's comment on why the
    // qmu::QmuParserError clause matters: VAbstractTool::CheckFormula(), called inside every
    // formula-bearing Create() below, rethrows it uncaught whenever qApp->isAppInGUIMode() is
    // false -- always true in this headless daemon).
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
            return ActionResult::failure(QStringLiteral("%1: unknown error creating the curve").arg(op));
        }
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
}

// Implements "spline": see curve_handlers.h for the documented JSON shape.
ActionResult handleSpline(const QJsonObject &args, const ActionContext &ctx)
{
    const QString point1Name = args.value(QStringLiteral("point1")).toString();
    const QString point4Name = args.value(QStringLiteral("point4")).toString();
    if (point1Name.isEmpty() || point4Name.isEmpty())
    {
        return ActionResult::failure(QStringLiteral("spline requires non-empty \"point1\" and \"point4\""));
    }
    if (!args.contains(QStringLiteral("length1")) || !args.contains(QStringLiteral("length2")))
    {
        return ActionResult::failure(QStringLiteral("spline requires \"length1\" and \"length2\" formulas"));
    }

    VContainer *data = ctx.data();
    VAbstractPattern *doc = ctx.doc();
    VMainGraphicsScene *scene = ctx.scene();
    if (data == nullptr || doc == nullptr || scene == nullptr)
    {
        return ActionResult::failure(QStringLiteral("spline: context is missing a scene, document, or data container"));
    }

    const quint32 point1Id = NameResolver::idForName(point1Name, data, Draw::Calculation);
    const quint32 point4Id = NameResolver::idForName(point4Name, data, Draw::Calculation);
    const QString point1TypeError = checkIsPoint(data, point1Id, QStringLiteral("point1"), point1Name);
    if (!point1TypeError.isEmpty())
    {
        return ActionResult::failure(point1TypeError);
    }
    const QString point4TypeError = checkIsPoint(data, point4Id, QStringLiteral("point4"), point4Name);
    if (!point4TypeError.isEmpty())
    {
        return ActionResult::failure(point4TypeError);
    }

    QString a1 = args.contains(QStringLiteral("angle1")) ? args.value(QStringLiteral("angle1")).toString() : QStringLiteral("0");
    QString a2 = args.contains(QStringLiteral("angle2")) ? args.value(QStringLiteral("angle2")).toString() : QStringLiteral("0");
    QString l1 = args.value(QStringLiteral("length1")).toString();
    QString l2 = args.value(QStringLiteral("length2")).toString();
    const quint32 duplicate = static_cast<quint32>(args.value(QStringLiteral("duplicate")).toInt(0));
    const QString color = lineColorOrDefault(args);
    const QString penStyle = lineTypeOrDefault(args);
    const QString lineWeight = lineWeightOrDefault(args);
    const bool autoSmooth = args.value(QStringLiteral("autoSmooth")).toBool(false);
    const int lengthMode = args.value(QStringLiteral("lengthMode")).toInt(0);
    const QString targetLength = args.value(QStringLiteral("targetLength")).toString();

    return runCreate(QStringLiteral("spline"), [&]() -> ActionResult {
        VToolSpline *tool = VToolSpline::Create(0, point1Id, point4Id, a1, a2, l1, l2, duplicate, color, penStyle,
                                                 lineWeight, scene, doc, data, Document::FullParse, Source::FromGui,
                                                 autoSmooth, lengthMode, targetLength);
        if (tool == nullptr)
        {
            return ActionResult::failure(QStringLiteral("spline: tool creation failed for an unknown reason"));
        }

        QJsonObject payload;
        payload["id"] = static_cast<qint64>(tool->getId());
        payload["point1"] = point1Name;
        payload["point4"] = point4Name;
        payload["op"] = QStringLiteral("spline");
        return ActionResult::success(payload);
    });
}

// Implements "splinePath": see curve_handlers.h for the documented JSON shape.
ActionResult handleSplinePath(const QJsonObject &args, const ActionContext &ctx)
{
    const QJsonArray pointsArg = args.value(QStringLiteral("points")).toArray();
    if (pointsArg.size() < 2)
    {
        return ActionResult::failure(QStringLiteral("splinePath requires at least two entries in \"points\""));
    }

    VContainer *data = ctx.data();
    VAbstractPattern *doc = ctx.doc();
    VMainGraphicsScene *scene = ctx.scene();
    if (data == nullptr || doc == nullptr || scene == nullptr)
    {
        return ActionResult::failure(QStringLiteral("splinePath: context is missing a scene, document, or data container"));
    }

    QVector<quint32> pointIds;
    QVector<QString> a1;
    QVector<QString> a2;
    QVector<QString> l1;
    QVector<QString> l2;
    QJsonArray pointNamesEcho;

    for (int i = 0; i < pointsArg.size(); ++i)
    {
        const QJsonObject entry = pointsArg.at(i).toObject();
        const QString pointName = entry.value(QStringLiteral("point")).toString();
        if (pointName.isEmpty())
        {
            return ActionResult::failure(QStringLiteral("splinePath: entry %1 requires a non-empty \"point\"").arg(i));
        }
        if (!entry.contains(QStringLiteral("length1")) || !entry.contains(QStringLiteral("length2")))
        {
            return ActionResult::failure(
                QStringLiteral("splinePath: entry %1 (\"%2\") requires \"length1\" and \"length2\" formulas").arg(i).arg(pointName));
        }

        const quint32 pointId = NameResolver::idForName(pointName, data, Draw::Calculation);
        const QString typeError = checkIsPoint(data, pointId, QStringLiteral("points"), pointName);
        if (!typeError.isEmpty())
        {
            return ActionResult::failure(typeError);
        }

        pointIds.append(pointId);
        a1.append(entry.contains(QStringLiteral("angle1")) ? entry.value(QStringLiteral("angle1")).toString() : QStringLiteral("0"));
        a2.append(entry.contains(QStringLiteral("angle2")) ? entry.value(QStringLiteral("angle2")).toString() : QStringLiteral("0"));
        l1.append(entry.value(QStringLiteral("length1")).toString());
        l2.append(entry.value(QStringLiteral("length2")).toString());
        pointNamesEcho.append(pointName);
    }

    const QString color = lineColorOrDefault(args);
    const QString penStyle = lineTypeOrDefault(args);
    const QString lineWeight = lineWeightOrDefault(args);
    const quint32 duplicate = static_cast<quint32>(args.value(QStringLiteral("duplicate")).toInt(0));

    return runCreate(QStringLiteral("splinePath"), [&]() -> ActionResult {
        VToolSplinePath *tool = VToolSplinePath::Create(0, pointIds, a1, a2, l1, l2, color, penStyle, lineWeight,
                                                          duplicate, scene, doc, data, Document::FullParse,
                                                          Source::FromGui);
        if (tool == nullptr)
        {
            return ActionResult::failure(QStringLiteral("splinePath: tool creation failed for an unknown reason"));
        }

        QJsonObject payload;
        payload["id"] = static_cast<qint64>(tool->getId());
        payload["points"] = pointNamesEcho;
        payload["op"] = QStringLiteral("splinePath");
        return ActionResult::success(payload);
    });
}

// Implements "cubicBezier": see curve_handlers.h for the documented JSON shape.
ActionResult handleCubicBezier(const QJsonObject &args, const ActionContext &ctx)
{
    const QString point1Name = args.value(QStringLiteral("point1")).toString();
    const QString point2Name = args.value(QStringLiteral("point2")).toString();
    const QString point3Name = args.value(QStringLiteral("point3")).toString();
    const QString point4Name = args.value(QStringLiteral("point4")).toString();
    if (point1Name.isEmpty() || point2Name.isEmpty() || point3Name.isEmpty() || point4Name.isEmpty())
    {
        return ActionResult::failure(
            QStringLiteral("cubicBezier requires non-empty \"point1\", \"point2\", \"point3\", and \"point4\""));
    }

    VContainer *data = ctx.data();
    VAbstractPattern *doc = ctx.doc();
    VMainGraphicsScene *scene = ctx.scene();
    if (data == nullptr || doc == nullptr || scene == nullptr)
    {
        return ActionResult::failure(QStringLiteral("cubicBezier: context is missing a scene, document, or data container"));
    }

    const quint32 point1Id = NameResolver::idForName(point1Name, data, Draw::Calculation);
    const quint32 point2Id = NameResolver::idForName(point2Name, data, Draw::Calculation);
    const quint32 point3Id = NameResolver::idForName(point3Name, data, Draw::Calculation);
    const quint32 point4Id = NameResolver::idForName(point4Name, data, Draw::Calculation);

    for (const auto &pair : { std::make_pair(point1Id, point1Name), std::make_pair(point2Id, point2Name),
                              std::make_pair(point3Id, point3Name), std::make_pair(point4Id, point4Name) })
    {
        const QString typeError = checkIsPoint(data, pair.first, QStringLiteral("points"), pair.second);
        if (!typeError.isEmpty())
        {
            return ActionResult::failure(typeError);
        }
    }

    try
    {
        const QSharedPointer<VPointF> p1 = data->GeometricObject<VPointF>(point1Id);
        const QSharedPointer<VPointF> p2 = data->GeometricObject<VPointF>(point2Id);
        const QSharedPointer<VPointF> p3 = data->GeometricObject<VPointF>(point3Id);
        const QSharedPointer<VPointF> p4 = data->GeometricObject<VPointF>(point4Id);

        // Create() below takes ownership via VContainer::AddGObject(), so no manual delete is
        // needed on the success path; every return before Create() runs happens before this
        // allocation, so no failure path can leak it.
        VCubicBezier *curve = new VCubicBezier(*p1, *p2, *p3, *p4);
        curve->setLineColor(lineColorOrDefault(args));
        curve->SetPenStyle(lineTypeOrDefault(args));
        curve->setLineWeight(lineWeightOrDefault(args));

        VToolCubicBezier *tool = VToolCubicBezier::Create(0, curve, false, 0, QString(), scene, doc, data,
                                                            Document::FullParse, Source::FromGui);
        if (tool == nullptr)
        {
            return ActionResult::failure(QStringLiteral("cubicBezier: tool creation failed for an unknown reason"));
        }

        QJsonObject payload;
        payload["id"] = static_cast<qint64>(tool->getId());
        payload["point1"] = point1Name;
        payload["point4"] = point4Name;
        payload["op"] = QStringLiteral("cubicBezier");
        return ActionResult::success(payload);
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
        return ActionResult::failure(QStringLiteral("cubicBezier: unknown error creating the curve"));
    }
}

// Implements "cubicBezierPath": see curve_handlers.h for the documented JSON shape.
ActionResult handleCubicBezierPath(const QJsonObject &args, const ActionContext &ctx)
{
    const QJsonArray pointsArg = args.value(QStringLiteral("points")).toArray();
    // 4 points for one segment, +3 per additional segment: 4, 7, 10, ... i.e. (n - 4) % 3 == 0.
    if (pointsArg.size() < 4 || (pointsArg.size() - 4) % 3 != 0)
    {
        return ActionResult::failure(
            QStringLiteral("cubicBezierPath requires \"points\" with 4, 7, 10, ... entries (4 + 3 per extra segment)"));
    }

    VContainer *data = ctx.data();
    VAbstractPattern *doc = ctx.doc();
    VMainGraphicsScene *scene = ctx.scene();
    if (data == nullptr || doc == nullptr || scene == nullptr)
    {
        return ActionResult::failure(QStringLiteral("cubicBezierPath: context is missing a scene, document, or data container"));
    }

    QVector<VPointF> points;
    QJsonArray pointNamesEcho;
    for (int i = 0; i < pointsArg.size(); ++i)
    {
        const QString pointName = pointsArg.at(i).toString();
        if (pointName.isEmpty())
        {
            return ActionResult::failure(QStringLiteral("cubicBezierPath: entry %1 is not a non-empty point name").arg(i));
        }
        const quint32 pointId = NameResolver::idForName(pointName, data, Draw::Calculation);
        const QString typeError = checkIsPoint(data, pointId, QStringLiteral("points"), pointName);
        if (!typeError.isEmpty())
        {
            return ActionResult::failure(typeError);
        }
        points.append(*data->GeometricObject<VPointF>(pointId));
        pointNamesEcho.append(pointName);
    }

    try
    {
        VCubicBezierPath *path = new VCubicBezierPath(points);
        path->setLineColor(lineColorOrDefault(args));
        path->SetPenStyle(lineTypeOrDefault(args));
        path->setLineWeight(lineWeightOrDefault(args));

        VToolCubicBezierPath *tool = VToolCubicBezierPath::Create(0, path, scene, doc, data, Document::FullParse,
                                                                     Source::FromGui);
        if (tool == nullptr)
        {
            return ActionResult::failure(QStringLiteral("cubicBezierPath: tool creation failed for an unknown reason"));
        }

        QJsonObject payload;
        payload["id"] = static_cast<qint64>(tool->getId());
        payload["points"] = pointNamesEcho;
        payload["op"] = QStringLiteral("cubicBezierPath");
        return ActionResult::success(payload);
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
        return ActionResult::failure(QStringLiteral("cubicBezierPath: unknown error creating the curve"));
    }
}

// Implements "arc": see curve_handlers.h for the documented JSON shape.
ActionResult handleArc(const QJsonObject &args, const ActionContext &ctx)
{
    const QString centerName = args.value(QStringLiteral("center")).toString();
    if (centerName.isEmpty())
    {
        return ActionResult::failure(QStringLiteral("arc requires a non-empty \"center\""));
    }
    if (!args.contains(QStringLiteral("radius")) || !args.contains(QStringLiteral("f1"))
        || !args.contains(QStringLiteral("f2")))
    {
        return ActionResult::failure(QStringLiteral("arc requires \"radius\", \"f1\", and \"f2\" formulas"));
    }

    VContainer *data = ctx.data();
    VAbstractPattern *doc = ctx.doc();
    VMainGraphicsScene *scene = ctx.scene();
    if (data == nullptr || doc == nullptr || scene == nullptr)
    {
        return ActionResult::failure(QStringLiteral("arc: context is missing a scene, document, or data container"));
    }

    const quint32 centerId = NameResolver::idForName(centerName, data, Draw::Calculation);
    const QString typeError = checkIsPoint(data, centerId, QStringLiteral("center"), centerName);
    if (!typeError.isEmpty())
    {
        return ActionResult::failure(typeError);
    }

    QString radius = args.value(QStringLiteral("radius")).toString();
    QString f1 = args.value(QStringLiteral("f1")).toString();
    QString f2 = args.value(QStringLiteral("f2")).toString();
    const QString color = lineColorOrDefault(args);
    const QString penStyle = lineTypeOrDefault(args);
    const QString lineWeight = lineWeightOrDefault(args);

    return runCreate(QStringLiteral("arc"), [&]() -> ActionResult {
        VToolArc *tool = VToolArc::Create(0, centerId, radius, f1, f2, color, penStyle, lineWeight, scene, doc, data,
                                           Document::FullParse, Source::FromGui);
        if (tool == nullptr)
        {
            return ActionResult::failure(QStringLiteral("arc: tool creation failed for an unknown reason"));
        }

        QJsonObject payload;
        payload["id"] = static_cast<qint64>(tool->getId());
        payload["center"] = centerName;
        payload["op"] = QStringLiteral("arc");
        return ActionResult::success(payload);
    });
}

// Implements "arcWithLength": see curve_handlers.h for the documented JSON shape.
ActionResult handleArcWithLength(const QJsonObject &args, const ActionContext &ctx)
{
    const QString centerName = args.value(QStringLiteral("center")).toString();
    if (centerName.isEmpty())
    {
        return ActionResult::failure(QStringLiteral("arcWithLength requires a non-empty \"center\""));
    }
    if (!args.contains(QStringLiteral("radius")) || !args.contains(QStringLiteral("f1"))
        || !args.contains(QStringLiteral("length")))
    {
        return ActionResult::failure(QStringLiteral("arcWithLength requires \"radius\", \"f1\", and \"length\" formulas"));
    }

    VContainer *data = ctx.data();
    VAbstractPattern *doc = ctx.doc();
    VMainGraphicsScene *scene = ctx.scene();
    if (data == nullptr || doc == nullptr || scene == nullptr)
    {
        return ActionResult::failure(QStringLiteral("arcWithLength: context is missing a scene, document, or data container"));
    }

    const quint32 centerId = NameResolver::idForName(centerName, data, Draw::Calculation);
    const QString typeError = checkIsPoint(data, centerId, QStringLiteral("center"), centerName);
    if (!typeError.isEmpty())
    {
        return ActionResult::failure(typeError);
    }

    QString radius = args.value(QStringLiteral("radius")).toString();
    QString f1 = args.value(QStringLiteral("f1")).toString();
    QString length = args.value(QStringLiteral("length")).toString();
    const QString color = lineColorOrDefault(args);
    const QString penStyle = lineTypeOrDefault(args);
    const QString lineWeight = lineWeightOrDefault(args);

    return runCreate(QStringLiteral("arcWithLength"), [&]() -> ActionResult {
        VToolArcWithLength *tool = VToolArcWithLength::Create(0, centerId, radius, f1, length, color, penStyle,
                                                                lineWeight, scene, doc, data, Document::FullParse,
                                                                Source::FromGui);
        if (tool == nullptr)
        {
            return ActionResult::failure(QStringLiteral("arcWithLength: tool creation failed for an unknown reason"));
        }

        QJsonObject payload;
        payload["id"] = static_cast<qint64>(tool->getId());
        payload["center"] = centerName;
        payload["op"] = QStringLiteral("arcWithLength");
        return ActionResult::success(payload);
    });
}

// Implements "ellipticalArc": see curve_handlers.h for the documented JSON shape.
ActionResult handleEllipticalArc(const QJsonObject &args, const ActionContext &ctx)
{
    const QString centerName = args.value(QStringLiteral("center")).toString();
    if (centerName.isEmpty())
    {
        return ActionResult::failure(QStringLiteral("ellipticalArc requires a non-empty \"center\""));
    }
    if (!args.contains(QStringLiteral("radius1")) || !args.contains(QStringLiteral("radius2"))
        || !args.contains(QStringLiteral("f1")) || !args.contains(QStringLiteral("f2")))
    {
        return ActionResult::failure(
            QStringLiteral("ellipticalArc requires \"radius1\", \"radius2\", \"f1\", and \"f2\" formulas"));
    }

    VContainer *data = ctx.data();
    VAbstractPattern *doc = ctx.doc();
    VMainGraphicsScene *scene = ctx.scene();
    if (data == nullptr || doc == nullptr || scene == nullptr)
    {
        return ActionResult::failure(
            QStringLiteral("ellipticalArc: context is missing a scene, document, or data container"));
    }

    const quint32 centerId = NameResolver::idForName(centerName, data, Draw::Calculation);
    const QString typeError = checkIsPoint(data, centerId, QStringLiteral("center"), centerName);
    if (!typeError.isEmpty())
    {
        return ActionResult::failure(typeError);
    }

    QString radius1 = args.value(QStringLiteral("radius1")).toString();
    QString radius2 = args.value(QStringLiteral("radius2")).toString();
    QString f1 = args.value(QStringLiteral("f1")).toString();
    QString f2 = args.value(QStringLiteral("f2")).toString();
    QString rotationAngle = args.contains(QStringLiteral("rotationAngle"))
                                 ? args.value(QStringLiteral("rotationAngle")).toString()
                                 : QStringLiteral("0");
    const QString color = lineColorOrDefault(args);
    const QString penStyle = lineTypeOrDefault(args);
    const QString lineWeight = lineWeightOrDefault(args);

    return runCreate(QStringLiteral("ellipticalArc"), [&]() -> ActionResult {
        VToolEllipticalArc *tool = VToolEllipticalArc::Create(0, centerId, radius1, radius2, f1, f2, rotationAngle,
                                                                 color, penStyle, lineWeight, scene, doc, data,
                                                                 Document::FullParse, Source::FromGui);
        if (tool == nullptr)
        {
            return ActionResult::failure(QStringLiteral("ellipticalArc: tool creation failed for an unknown reason"));
        }

        QJsonObject payload;
        payload["id"] = static_cast<qint64>(tool->getId());
        payload["center"] = centerName;
        payload["op"] = QStringLiteral("ellipticalArc");
        return ActionResult::success(payload);
    });
}
