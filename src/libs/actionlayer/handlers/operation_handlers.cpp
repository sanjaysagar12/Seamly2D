//---------------------------------------------------------------------------------------------------------------------
//  @file   operation_handlers.cpp
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

#include "operation_handlers.h"

#include "../action_context.h"
#include "../name_resolver.h"

#include "../../vpatterndb/vcontainer.h"
#include "../../vgeometry/vgobject.h"
#include "../../vgeometry/vgeometrydef.h"
#include "../../ifc/xml/vabstractpattern.h"
#include "../../ifc/exception/vexception.h"
#include "../../ifc/ifcdef.h"
#include "../../qmuparser/qmuparsererror.h"
#include "../../vmisc/vabstractapplication.h"
#include "../../vwidgets/vmaingraphicsscene.h"
#include "../../vwidgets/vmaingraphicsview.h"

#include "../../vtools/tools/drawTools/operation/vabstractoperation.h"
#include "../../vtools/tools/drawTools/operation/vtoolmove.h"
#include "../../vtools/tools/drawTools/operation/vtoolrotation.h"
#include "../../vtools/tools/drawTools/operation/mirror/vtoolmirrorbyline.h"
#include "../../vtools/tools/drawTools/operation/mirror/vtoolmirrorbyaxis.h"
#include "../../vtools/tools/drawTools/toolpoint/tooldoublepoint/vtooltruedarts.h"

#include <algorithm>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QSet>
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
            return ActionResult::failure(QStringLiteral("%1: unknown error running the operation").arg(op));
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

    // Resolves "sourceObjects" (a JSON array of names) into a QVector<SourceItem>, applying this
    // op's lineType/lineWeight/lineColor to every entry uniformly (the JSON shape does not support
    // per-source-item style overrides, keeping this a thin translation rather than a second dialog).
    // Returns an empty vector (and fills outError) if "sourceObjects" is missing/empty/malformed.
    bool resolveSourceObjects(const QJsonObject &args, const VContainer *data, const QString &op,
                              QVector<SourceItem> &outSource, QJsonArray &outNamesEcho, QString &outError)
    {
        const QJsonArray namesArg = args.value(QStringLiteral("sourceObjects")).toArray();
        if (namesArg.isEmpty())
        {
            outError = QStringLiteral("%1 requires a non-empty \"sourceObjects\" array").arg(op);
            return false;
        }

        const QString lineType = lineTypeOrDefault(args);
        const QString lineWeight = lineWeightOrDefault(args);
        const QString lineColor = lineColorOrDefault(args);

        for (const QJsonValue &nameValue : namesArg)
        {
            const QString name = nameValue.toString();
            if (name.isEmpty())
            {
                outError = QStringLiteral("%1: \"sourceObjects\" contains a non-string/empty entry").arg(op);
                return false;
            }
            const quint32 id = NameResolver::idForName(name, data, Draw::Calculation); // Uncaught by design; ActionEngine's ActionResolverError clause reports it.

            SourceItem item;
            item.id = id;
            item.lineType = lineType;
            item.lineWidth = lineWeight;
            item.color = lineColor;
            outSource.append(item);
            outNamesEcho.append(name);
        }
        return true;
    }

    // VAbstractOperation's "destination" vector (populated by Create() for Source::FromGui) is a
    // protected member with no public getter, so the newly-created objects are instead recovered
    // by diffing VContainer::DataGObjects()'s id set before/after the Create() call and sorting
    // the new ids ascending. This is safe because every one of these four tools' raw Create()
    // overloads shares the exact same "dest.clear(); id = VContainer::getNextId(); for (i = 0; i <
    // source.size(); ++i) dest.append(createPoint/createItem(...))" loop (verified against
    // VToolMove::Create() and VToolRotation::Create()): each new destination object's id is
    // allocated by VContainer::getNextId()/AddGObject() in the same order the loop walks `source`,
    // so ascending-sorted new ids are guaranteed to line up with the input "sourceObjects" order.
    QVector<quint32> newObjectIds(const QSet<quint32> &beforeIds, const VContainer *data)
    {
        QVector<quint32> result;
        const QHash<quint32, QSharedPointer<VGObject>> *gObjects = data->DataGObjects();
        if (gObjects != nullptr)
        {
            for (auto it = gObjects->constBegin(); it != gObjects->constEnd(); ++it)
            {
                if (!beforeIds.contains(it.key()))
                {
                    result.append(it.key());
                }
            }
        }
        std::sort(result.begin(), result.end());
        return result;
    }

    // Builds the documented {"created":[{"id","name"}, ...],"op"} payload from a set of newly
    // created object ids (see newObjectIds() above for how they are recovered and why their sorted
    // order matches the input "sourceObjects" order).
    QJsonObject buildCreatedPayload(const QString &op, const QVector<quint32> &createdIds, const VContainer *data)
    {
        QJsonArray created;
        for (quint32 id : createdIds)
        {
            QJsonObject entry;
            entry["id"] = static_cast<qint64>(id);
            const QSharedPointer<VGObject> obj = data->GetGObject(id);
            entry["name"] = obj.isNull() ? QString() : obj->name();
            created.append(entry);
        }
        QJsonObject payload;
        payload["created"] = created;
        payload["op"] = op;
        return payload;
    }
}

// Implements "move": see operation_handlers.h for the documented JSON shape.
ActionResult handleMove(const QJsonObject &args, const ActionContext &ctx)
{
    const QString suffix = args.value(QStringLiteral("suffix")).toString();
    if (suffix.isEmpty())
    {
        return ActionResult::failure(QStringLiteral("move requires a non-empty \"suffix\""));
    }
    if (!args.contains(QStringLiteral("length")) || !args.contains(QStringLiteral("angle"))
        || !args.contains(QStringLiteral("rotationAngle")))
    {
        return ActionResult::failure(
            QStringLiteral("move requires \"length\", \"angle\", and \"rotationAngle\" formulas"));
    }

    VContainer *data = ctx.data();
    VAbstractPattern *doc = ctx.doc();
    VMainGraphicsScene *scene = ctx.scene();
    if (data == nullptr || doc == nullptr || scene == nullptr)
    {
        return ActionResult::failure(QStringLiteral("move: context is missing a scene, document, or data container"));
    }

    QVector<SourceItem> source;
    QJsonArray sourceNamesEcho;
    QString sourceError;
    if (!resolveSourceObjects(args, data, QStringLiteral("move"), source, sourceNamesEcho, sourceError))
    {
        return ActionResult::failure(sourceError);
    }

    quint32 originPointId = NULL_ID;
    if (args.contains(QStringLiteral("rotationOrigin")))
    {
        const QString originName = args.value(QStringLiteral("rotationOrigin")).toString();
        originPointId = NameResolver::idForName(originName, data, Draw::Calculation);
        const QString typeError = checkIsPoint(data, originPointId, QStringLiteral("rotationOrigin"), originName);
        if (!typeError.isEmpty())
        {
            return ActionResult::failure(typeError);
        }
    }

    QString formulaLength = args.value(QStringLiteral("length")).toString();
    QString formulaAngle = args.value(QStringLiteral("angle")).toString();
    QString formulaRotation = args.value(QStringLiteral("rotationAngle")).toString();

    return runCreate(QStringLiteral("move"), [&]() -> ActionResult {
        const QList<quint32> beforeIdsList = data->DataGObjects()->keys();
        const QSet<quint32> beforeIds(beforeIdsList.begin(), beforeIdsList.end());
        VToolMove *tool = VToolMove::Create(0, formulaAngle, formulaLength, formulaRotation, originPointId, suffix,
                                             source, QVector<DestinationItem>(), scene, doc, data,
                                             Document::FullParse, Source::FromGui);
        if (tool == nullptr)
        {
            return ActionResult::failure(QStringLiteral("move: tool creation failed for an unknown reason"));
        }
        return ActionResult::success(buildCreatedPayload(QStringLiteral("move"), newObjectIds(beforeIds, data), data));
    });
}

// Implements "rotation": see operation_handlers.h for the documented JSON shape.
ActionResult handleRotation(const QJsonObject &args, const ActionContext &ctx)
{
    const QString suffix = args.value(QStringLiteral("suffix")).toString();
    if (suffix.isEmpty())
    {
        return ActionResult::failure(QStringLiteral("rotation requires a non-empty \"suffix\""));
    }
    const QString originName = args.value(QStringLiteral("origin")).toString();
    if (originName.isEmpty())
    {
        return ActionResult::failure(QStringLiteral("rotation requires a non-empty \"origin\""));
    }
    if (!args.contains(QStringLiteral("angle")))
    {
        return ActionResult::failure(QStringLiteral("rotation requires an \"angle\" formula"));
    }

    VContainer *data = ctx.data();
    VAbstractPattern *doc = ctx.doc();
    VMainGraphicsScene *scene = ctx.scene();
    if (data == nullptr || doc == nullptr || scene == nullptr)
    {
        return ActionResult::failure(QStringLiteral("rotation: context is missing a scene, document, or data container"));
    }

    const quint32 originId = NameResolver::idForName(originName, data, Draw::Calculation);
    const QString originTypeError = checkIsPoint(data, originId, QStringLiteral("origin"), originName);
    if (!originTypeError.isEmpty())
    {
        return ActionResult::failure(originTypeError);
    }

    QVector<SourceItem> source;
    QJsonArray sourceNamesEcho;
    QString sourceError;
    if (!resolveSourceObjects(args, data, QStringLiteral("rotation"), source, sourceNamesEcho, sourceError))
    {
        return ActionResult::failure(sourceError);
    }

    QString formulaAngle = args.value(QStringLiteral("angle")).toString();

    return runCreate(QStringLiteral("rotation"), [&]() -> ActionResult {
        const QList<quint32> beforeIdsList = data->DataGObjects()->keys();
        const QSet<quint32> beforeIds(beforeIdsList.begin(), beforeIdsList.end());
        VToolRotation *tool = VToolRotation::Create(0, originId, formulaAngle, suffix, source,
                                                     QVector<DestinationItem>(), scene, doc, data,
                                                     Document::FullParse, Source::FromGui);
        if (tool == nullptr)
        {
            return ActionResult::failure(QStringLiteral("rotation: tool creation failed for an unknown reason"));
        }
        return ActionResult::success(
            buildCreatedPayload(QStringLiteral("rotation"), newObjectIds(beforeIds, data), data));
    });
}

// Implements "mirrorByLine": see operation_handlers.h for the documented JSON shape.
ActionResult handleMirrorByLine(const QJsonObject &args, const ActionContext &ctx)
{
    const QString suffix = args.value(QStringLiteral("suffix")).toString();
    if (suffix.isEmpty())
    {
        return ActionResult::failure(QStringLiteral("mirrorByLine requires a non-empty \"suffix\""));
    }
    const QString firstName = args.value(QStringLiteral("firstLinePoint")).toString();
    const QString secondName = args.value(QStringLiteral("secondLinePoint")).toString();
    if (firstName.isEmpty() || secondName.isEmpty())
    {
        return ActionResult::failure(
            QStringLiteral("mirrorByLine requires non-empty \"firstLinePoint\" and \"secondLinePoint\""));
    }

    VContainer *data = ctx.data();
    VAbstractPattern *doc = ctx.doc();
    VMainGraphicsScene *scene = ctx.scene();
    if (data == nullptr || doc == nullptr || scene == nullptr)
    {
        return ActionResult::failure(
            QStringLiteral("mirrorByLine: context is missing a scene, document, or data container"));
    }

    const quint32 firstId = NameResolver::idForName(firstName, data, Draw::Calculation);
    const quint32 secondId = NameResolver::idForName(secondName, data, Draw::Calculation);
    const QString firstTypeError = checkIsPoint(data, firstId, QStringLiteral("firstLinePoint"), firstName);
    if (!firstTypeError.isEmpty())
    {
        return ActionResult::failure(firstTypeError);
    }
    const QString secondTypeError = checkIsPoint(data, secondId, QStringLiteral("secondLinePoint"), secondName);
    if (!secondTypeError.isEmpty())
    {
        return ActionResult::failure(secondTypeError);
    }

    QVector<SourceItem> source;
    QJsonArray sourceNamesEcho;
    QString sourceError;
    if (!resolveSourceObjects(args, data, QStringLiteral("mirrorByLine"), source, sourceNamesEcho, sourceError))
    {
        return ActionResult::failure(sourceError);
    }

    return runCreate(QStringLiteral("mirrorByLine"), [&]() -> ActionResult {
        const QList<quint32> beforeIdsList = data->DataGObjects()->keys();
        const QSet<quint32> beforeIds(beforeIdsList.begin(), beforeIdsList.end());
        VToolMirrorByLine *tool = VToolMirrorByLine::Create(0, firstId, secondId, suffix, source,
                                                             QVector<DestinationItem>(), scene, doc, data,
                                                             Document::FullParse, Source::FromGui);
        if (tool == nullptr)
        {
            return ActionResult::failure(QStringLiteral("mirrorByLine: tool creation failed for an unknown reason"));
        }
        return ActionResult::success(
            buildCreatedPayload(QStringLiteral("mirrorByLine"), newObjectIds(beforeIds, data), data));
    });
}

// Implements "mirrorByAxis": see operation_handlers.h for the documented JSON shape.
ActionResult handleMirrorByAxis(const QJsonObject &args, const ActionContext &ctx)
{
    const QString suffix = args.value(QStringLiteral("suffix")).toString();
    if (suffix.isEmpty())
    {
        return ActionResult::failure(QStringLiteral("mirrorByAxis requires a non-empty \"suffix\""));
    }
    const QString originName = args.value(QStringLiteral("originPoint")).toString();
    if (originName.isEmpty())
    {
        return ActionResult::failure(QStringLiteral("mirrorByAxis requires a non-empty \"originPoint\""));
    }
    const QString axisRaw = args.value(QStringLiteral("axisType")).toString();
    AxisType axisType;
    if (axisRaw.compare(QStringLiteral("vertical"), Qt::CaseInsensitive) == 0)
    {
        axisType = AxisType::VerticalAxis;
    }
    else if (axisRaw.compare(QStringLiteral("horizontal"), Qt::CaseInsensitive) == 0)
    {
        axisType = AxisType::HorizontalAxis;
    }
    else
    {
        return ActionResult::failure(
            QStringLiteral("mirrorByAxis requires \"axisType\" to be \"vertical\" or \"horizontal\" (got \"%1\")").arg(axisRaw));
    }

    VContainer *data = ctx.data();
    VAbstractPattern *doc = ctx.doc();
    VMainGraphicsScene *scene = ctx.scene();
    if (data == nullptr || doc == nullptr || scene == nullptr)
    {
        return ActionResult::failure(
            QStringLiteral("mirrorByAxis: context is missing a scene, document, or data container"));
    }

    const quint32 originId = NameResolver::idForName(originName, data, Draw::Calculation);
    const QString originTypeError = checkIsPoint(data, originId, QStringLiteral("originPoint"), originName);
    if (!originTypeError.isEmpty())
    {
        return ActionResult::failure(originTypeError);
    }

    QVector<SourceItem> source;
    QJsonArray sourceNamesEcho;
    QString sourceError;
    if (!resolveSourceObjects(args, data, QStringLiteral("mirrorByAxis"), source, sourceNamesEcho, sourceError))
    {
        return ActionResult::failure(sourceError);
    }

    return runCreate(QStringLiteral("mirrorByAxis"), [&]() -> ActionResult {
        const QList<quint32> beforeIdsList = data->DataGObjects()->keys();
        const QSet<quint32> beforeIds(beforeIdsList.begin(), beforeIdsList.end());
        VToolMirrorByAxis *tool = VToolMirrorByAxis::Create(0, originId, axisType, suffix, source,
                                                             QVector<DestinationItem>(), scene, doc, data,
                                                             Document::FullParse, Source::FromGui);
        if (tool == nullptr)
        {
            return ActionResult::failure(QStringLiteral("mirrorByAxis: tool creation failed for an unknown reason"));
        }
        return ActionResult::success(
            buildCreatedPayload(QStringLiteral("mirrorByAxis"), newObjectIds(beforeIds, data), data));
    });
}

// Implements "group": see operation_handlers.h for the documented JSON shape. Reproduces
// AddGroup::redo()'s (vtools/undocommands/addgroup.cpp) non-undo-stack logic directly: every
// other mutating handler in this action layer already calls a VTool::Create() straight through
// without pushing onto qApp->getUndoStack() (see point_handlers.cpp's handleBasePoint()), so doing
// the same for the doc-level group bookkeeping here -- rather than wrapping it in an AddGroup
// QUndoCommand meant for the interactive GUI's undo/redo stack -- keeps this handler consistent
// with the rest of the file.
ActionResult handleGroup(const QJsonObject &args, const ActionContext &ctx)
{
    const QString name = args.value(QStringLiteral("name")).toString();
    if (name.isEmpty())
    {
        return ActionResult::failure(QStringLiteral("group requires a non-empty \"name\""));
    }

    VContainer *data = ctx.data();
    VAbstractPattern *doc = ctx.doc();
    if (data == nullptr || doc == nullptr)
    {
        return ActionResult::failure(QStringLiteral("group: context is missing a document or data container"));
    }

    if (doc->groupNameExists(name))
    {
        QJsonObject error = structuredError(QStringLiteral("groupExists"),
            QStringLiteral("Group already exists: %1").arg(name));
        error["name"] = name;
        return ActionResult::failure(QJsonValue(error));
    }

    const QJsonArray namesArg = args.value(QStringLiteral("sourceObjects")).toArray();
    if (namesArg.isEmpty())
    {
        return ActionResult::failure(QStringLiteral("group requires a non-empty \"sourceObjects\" array"));
    }

    // groupData maps toolId -> objectId; using the same resolved id for both is the simple case
    // every plain (non-composite) selected object uses in the interactive group dialog -- see this
    // op's header comment for the documented gap this leaves for composite/multi-part tools.
    QMap<quint32, quint32> groupData;
    QJsonArray sourceNamesEcho;
    for (const QJsonValue &nameValue : namesArg)
    {
        const QString objName = nameValue.toString();
        if (objName.isEmpty())
        {
            return ActionResult::failure(QStringLiteral("group: \"sourceObjects\" contains a non-string/empty entry"));
        }
        const quint32 id = NameResolver::idForName(objName, data, Draw::Calculation); // Uncaught by design; see resolveSourceObjects()'s comment above.
        groupData.insert(id, id);
        sourceNamesEcho.append(objName);
    }

    const QString color = lineColorOrDefault(args);
    const QString lineType = lineTypeOrDefault(args);
    const QString lineWeight = lineWeightOrDefault(args);

    try
    {
        const quint32 groupId = VContainer::getNextId();
        const QDomElement groupElement = doc->createGroup(groupId, name, color, lineType, lineWeight, groupData);
        if (groupElement.isNull())
        {
            return ActionResult::failure(QStringLiteral("group: failed to build the group's DOM element"));
        }

        QDomElement groups = doc->createGroups();
        if (groups.isNull())
        {
            return ActionResult::failure(QStringLiteral("group: failed to obtain the pattern's <groups> element"));
        }
        groups.appendChild(groupElement);
        doc->parseGroups(groups);

        // Mirrors AddGroup::redo()'s own call, needed for the same reason action_host.cpp already
        // wires up qApp's current scene/view for every other mutating op (see that file's comment
        // on VMainGraphicsView::NewSceneRect()): several reused code paths unconditionally reach
        // for a non-null current scene/view.
        VMainGraphicsView::NewSceneRect(qApp->getCurrentScene(), qApp->getSceneView());

        QJsonObject payload;
        payload["id"] = static_cast<qint64>(groupId);
        payload["name"] = name;
        payload["sourceObjects"] = sourceNamesEcho;
        payload["op"] = QStringLiteral("group");
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
        return ActionResult::failure(QStringLiteral("group: unknown error creating the group"));
    }
}

// Implements "trueDarts": see operation_handlers.h for the documented JSON shape.
ActionResult handleTrueDarts(const QJsonObject &args, const ActionContext &ctx)
{
    const QString point1Name = args.value(QStringLiteral("point1Name")).toString();
    const QString point2Name = args.value(QStringLiteral("point2Name")).toString();
    if (point1Name.isEmpty() || point2Name.isEmpty())
    {
        return ActionResult::failure(QStringLiteral("trueDarts requires non-empty \"point1Name\" and \"point2Name\""));
    }
    const QString baseLineP1Name = args.value(QStringLiteral("baseLineP1")).toString();
    const QString baseLineP2Name = args.value(QStringLiteral("baseLineP2")).toString();
    const QString dartP1Name = args.value(QStringLiteral("dartP1")).toString();
    const QString dartP2Name = args.value(QStringLiteral("dartP2")).toString();
    const QString dartP3Name = args.value(QStringLiteral("dartP3")).toString();
    if (baseLineP1Name.isEmpty() || baseLineP2Name.isEmpty() || dartP1Name.isEmpty() || dartP2Name.isEmpty()
        || dartP3Name.isEmpty())
    {
        return ActionResult::failure(QStringLiteral(
            "trueDarts requires non-empty \"baseLineP1\", \"baseLineP2\", \"dartP1\", \"dartP2\", and \"dartP3\""));
    }

    VContainer *data = ctx.data();
    VAbstractPattern *doc = ctx.doc();
    VMainGraphicsScene *scene = ctx.scene();
    if (data == nullptr || doc == nullptr || scene == nullptr)
    {
        return ActionResult::failure(QStringLiteral("trueDarts: context is missing a scene, document, or data container"));
    }

    const quint32 baseLineP1Id = NameResolver::idForName(baseLineP1Name, data, Draw::Calculation);
    const quint32 baseLineP2Id = NameResolver::idForName(baseLineP2Name, data, Draw::Calculation);
    const quint32 dartP1Id = NameResolver::idForName(dartP1Name, data, Draw::Calculation);
    const quint32 dartP2Id = NameResolver::idForName(dartP2Name, data, Draw::Calculation);
    const quint32 dartP3Id = NameResolver::idForName(dartP3Name, data, Draw::Calculation);

    for (const auto &pair : { std::make_pair(baseLineP1Id, baseLineP1Name), std::make_pair(baseLineP2Id, baseLineP2Name),
                              std::make_pair(dartP1Id, dartP1Name), std::make_pair(dartP2Id, dartP2Name),
                              std::make_pair(dartP3Id, dartP3Name) })
    {
        const QString typeError = checkIsPoint(data, pair.first, QStringLiteral("points"), pair.second);
        if (!typeError.isEmpty())
        {
            return ActionResult::failure(typeError);
        }
    }

    const qreal mx1 = args.value(QStringLiteral("mx1")).toDouble(0.0);
    const qreal my1 = args.value(QStringLiteral("my1")).toDouble(0.0);
    const bool showPointName1 = args.value(QStringLiteral("showPointName1")).toBool(true);
    const qreal mx2 = args.value(QStringLiteral("mx2")).toDouble(0.0);
    const qreal my2 = args.value(QStringLiteral("my2")).toDouble(0.0);
    const bool showPointName2 = args.value(QStringLiteral("showPointName2")).toBool(true);

    try
    {
        VToolTrueDarts *tool = VToolTrueDarts::Create(
            0, NULL_ID, NULL_ID, baseLineP1Id, baseLineP2Id, dartP1Id, dartP2Id, dartP3Id, point1Name, mx1, my1,
            showPointName1, point2Name, mx2, my2, showPointName2, scene, doc, data, Document::FullParse,
            Source::FromGui);
        if (tool == nullptr)
        {
            return ActionResult::failure(QStringLiteral("trueDarts: tool creation failed for an unknown reason"));
        }

        QJsonObject payload;
        payload["id"] = static_cast<qint64>(tool->getId());
        payload["point1Name"] = point1Name;
        payload["point2Name"] = point2Name;
        payload["op"] = QStringLiteral("trueDarts");
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
        return ActionResult::failure(QStringLiteral("trueDarts: unknown error creating the true darts points"));
    }
}
