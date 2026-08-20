//---------------------------------------------------------------------------------------------------------------------
//  @file   piece_handlers.cpp
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

#include "piece_handlers.h"

#include "../action_context.h"
#include "../name_resolver.h"

#include "../../vpatterndb/vcontainer.h"
#include "../../vpatterndb/vpiece.h"
#include "../../vpatterndb/vpiecenode.h"
#include "../../vpatterndb/vpiecepath.h"
#include "../../vgeometry/vgobject.h"
#include "../../vgeometry/vgeometrydef.h"
#include "../../vgeometry/vpointf.h"
#include "../../ifc/xml/vabstractpattern.h"
#include "../../ifc/exception/vexception.h"
#include "../../ifc/ifcdef.h"
#include "../../qmuparser/qmuparsererror.h"
#include "../../vmisc/def.h"
#include "../../vmisc/vabstractapplication.h"
#include "../../vwidgets/vmaingraphicsscene.h"

#include "../../vtools/tools/vabstracttool.h"
#include "../../vtools/tools/pattern_piece_tool.h"
#include "../../vtools/tools/nodeDetails/anchorpoint_tool.h"
#include "../../vtools/tools/nodeDetails/internal_path_tool.h"
#include "../../vtools/tools/nodeDetails/vnodepoint.h"
#include "../../vtools/tools/union_tool.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QLineF>
#include <QPointF>
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
        // same-named Draw::Modeling piece-node clone can otherwise be resolved instead -- the
        // exact bug this whole file's node-cloning logic was written around).
        if (obj->getMode() != Draw::Calculation)
        {
            return QStringLiteral("\"%1\" (\"%2\") resolved to a %3 object, not a calculation-context point")
                .arg(fieldName, name, NameResolver::drawModeToString(obj->getMode()));
        }
        return QString();
    }

    // Pieces are not VGObjects (VContainer::DataGObjects() never contains them, so
    // NameResolver::idForName() cannot resolve a piece name) -- they live in their own
    // VContainer::DataPieces() hash, keyed by id, each carrying its own VPiece::GetName(). This is
    // the piece-specific equivalent of NameResolver::idForName(), scanning that hash instead.
    bool resolvePieceId(const QString &name, const VContainer *data, quint32 &outId)
    {
        const QHash<quint32, VPiece> *pieces = data->DataPieces();
        if (pieces == nullptr)
        {
            return false;
        }
        for (auto it = pieces->constBegin(); it != pieces->constEnd(); ++it)
        {
            if (it.value().GetName() == name)
            {
                outId = it.key();
                return true;
            }
        }
        return false;
    }

    // Resolves a JSON array of point names into a QVector<VPieceNode> (every node Tool::NodePoint,
    // reverse=false -- see this file's header comment on the documented arc/curve-node gap).
    // Returns an empty vector (and fills outError) on the first unresolved/wrong-type name.
    bool resolvePointNodes(const QJsonArray &namesArg, const VContainer *data, const QString &op,
                           QVector<VPieceNode> &outNodes, QVector<quint32> &outIds, QJsonArray &outNamesEcho,
                           QString &outError)
    {
        for (const QJsonValue &nameValue : namesArg)
        {
            const QString name = nameValue.toString();
            if (name.isEmpty())
            {
                outError = QStringLiteral("%1: \"nodes\" contains a non-string/empty entry").arg(op);
                return false;
            }
            const quint32 id = NameResolver::idForName(name, data, Draw::Calculation); // Uncaught by design; ActionEngine's ActionResolverError clause reports it.
            const QString typeError = checkIsPoint(data, id, QStringLiteral("nodes"), name);
            if (!typeError.isEmpty())
            {
                outError = typeError;
                return false;
            }
            outNodes.append(VPieceNode(id, Tool::NodePoint, false));
            outIds.append(id);
            outNamesEcho.append(name);
        }
        return true;
    }

    // Resolves a JSON array of point names into a QVector<VPieceNode> suitable for
    // PatternPieceTool::Create()'s or InternalPathTool::Create()'s raw (non-dialog) overload.
    //
    // A VPiece/VPiecePath's nodes are NOT allowed to reference a draft point's own id directly:
    // VAbstractTool::PrepareNode() (vabstracttool.cpp) -- which every *dialog* overload
    // (PatternPieceTool::Create(dialog,...), InternalPathTool::Create(dialog,...)) calls before
    // reaching the raw id-based overload this handler uses -- first clones the point into a fresh
    // VContainer object via VAbstractTool::CreateNode<VPointF>() and registers a VNodePoint tool
    // wrapping that clone (VNodePoint::Create()), and only THAT clone's id is a valid piece-path
    // node id. Skipping this step (i.e. building VPieceNode(originalPointId, ...) directly, as an
    // early version of this handler did) compiles and runs past Create() itself, but segfaults
    // (reproduced and root-caused against this build) once downstream code needs a live
    // VNodePoint at that id and finds none instead. VAbstractTool::PrepareNode()/PrepareNodes()
    // are themselves `protected`, so a free-function handler cannot call them directly; this
    // reproduces their exact two-call body (CreateNode<VPointF>() + VNodePoint::Create()) instead.
    //
    // "piece.insertNodes" is the one op in this file that does NOT need this: PatternPieceTool::
    // insertNodes() (pattern_piece_tool.cpp) calls PrepareNode() itself internally (legal there --
    // it runs inside a VAbstractTool subclass's own static member function), so that op's own
    // resolvePointNodes() (below) intentionally passes the original point ids straight through.
    bool resolvePreparedPointNodes(const QJsonArray &namesArg, VContainer *data, VAbstractPattern *doc,
                                   VMainGraphicsScene *scene, const QString &op, QVector<VPieceNode> &outNodes,
                                   QVector<quint32> &outOriginalIds, QJsonArray &outNamesEcho, QString &outError)
    {
        for (const QJsonValue &nameValue : namesArg)
        {
            const QString name = nameValue.toString();
            if (name.isEmpty())
            {
                outError = QStringLiteral("%1: \"nodes\" contains a non-string/empty entry").arg(op);
                return false;
            }
            const quint32 pointId = NameResolver::idForName(name, data, Draw::Calculation); // Uncaught by design; ActionEngine's ActionResolverError clause reports it.
            const QString typeError = checkIsPoint(data, pointId, QStringLiteral("nodes"), name);
            if (!typeError.isEmpty())
            {
                outError = typeError;
                return false;
            }

            const quint32 nodeId = VAbstractTool::CreateNode<VPointF>(data, pointId);
            // Defense-in-depth, kept intentionally even though NameResolver::idForName()'s scoped
            // (Draw::Calculation) overload -- used everywhere in this action layer that resolves a
            // calculation-context name, including the lookup just above -- now makes this rename
            // unnecessary on its own: a scoped lookup filters on getMode() before ever comparing
            // names, so it architecturally cannot match a Draw::Modeling clone regardless of what
            // that clone is named. This rename predates the scoped overload (originally the *only*
            // fix, for the in-session case only -- see git history / name_resolver.h's own
            // "FOUND AND PARTIALLY FIXED" comment for the full story of how the scoped overload
            // superseded it, including for the reload case this rename never reached). Left in
            // place as a second, independent safety net: CreateNode<VPointF>() copy-constructs the
            // clone from its source (VGObject's copy constructor copies every field, including
            // name()), so without this rename the clone would still answer to the same name as its
            // source for any *unscoped* NameResolver::idForName() call elsewhere in the codebase
            // (e.g. "pattern.resolveName", deliberately unscoped -- see
            // pattern_resolve_name_handler.cpp) or any future one that forgets to scope. A
            // modeling-type clone's name is never written to the saved XML in the first place (see
            // e.g. cases/01_square/expected/square.val's <point type="modeling"> elements, which
            // carry no "name" attribute at all), so this remains a purely action-layer-local,
            // zero-effect-on-the-saved-file change either way.
            if (QSharedPointer<VGObject> clone = data->GetGObject(nodeId))
            {
                clone->setName(QStringLiteral("__pieceNode_%1").arg(nodeId));
            }
            VNodePoint::Create(doc, data, scene, nodeId, pointId, Document::FullParse, Source::FromGui);

            outNodes.append(VPieceNode(nodeId, Tool::NodePoint, false));
            outOriginalIds.append(pointId);
            outNamesEcho.append(name);
        }
        return true;
    }

    // Reports whether the closed polygon formed by `points` (in order, wrapping from the last
    // point back to the first) has any pair of non-adjacent edges that cross, or any
    // zero-length (duplicate consecutive point) edge -- either of which would make
    // PatternPieceTool::Create() build a self-intersecting or degenerate piece outline.
    bool isSimpleClosedPolygon(const QVector<QPointF> &points)
    {
        const int n = points.size();
        for (int i = 0; i < n; ++i)
        {
            const QLineF edgeI(points.at(i), points.at((i + 1) % n));
            if (edgeI.p1() == edgeI.p2())
            {
                return false; // Zero-length edge: two consecutive nodes resolve to the same point.
            }
            for (int j = i + 1; j < n; ++j)
            {
                if (i == j || (i + 1) % n == j || (j + 1) % n == i)
                {
                    continue; // Adjacent (or the same) edges always share an endpoint; not a real crossing.
                }
                const QLineF edgeJ(points.at(j), points.at((j + 1) % n));
                QPointF intersection;
                if (edgeI.intersects(edgeJ, &intersection) == QLineF::BoundedIntersection)
                {
                    return false;
                }
            }
        }
        return true;
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

// Implements "piece.addPatternPiece": see piece_handlers.h for the documented JSON shape.
ActionResult handlePieceAddPatternPiece(const QJsonObject &args, const ActionContext &ctx)
{
    const QString name = args.value(QStringLiteral("name")).toString();
    if (name.isEmpty())
    {
        return ActionResult::failure(QStringLiteral("piece.addPatternPiece requires a non-empty \"name\""));
    }
    const QJsonArray nodesArg = args.value(QStringLiteral("nodes")).toArray();
    if (nodesArg.size() < 3)
    {
        return ActionResult::failure(QStringLiteral("piece.addPatternPiece requires at least 3 entries in \"nodes\""));
    }
    if (!args.contains(QStringLiteral("seamAllowanceWidth")))
    {
        return ActionResult::failure(QStringLiteral("piece.addPatternPiece requires a \"seamAllowanceWidth\" formula"));
    }

    VContainer *data = ctx.data();
    VAbstractPattern *doc = ctx.doc();
    VMainGraphicsScene *pieceScene = ctx.pieceScene();
    if (data == nullptr || doc == nullptr || pieceScene == nullptr)
    {
        return ActionResult::failure(
            QStringLiteral("piece.addPatternPiece: context is missing a piece scene, document, or data container"));
    }

    QVector<VPieceNode> nodes;
    QVector<quint32> originalPointIds;
    QJsonArray nodeNamesEcho;
    QString resolveError;
    if (!resolvePreparedPointNodes(nodesArg, data, doc, pieceScene, QStringLiteral("piece.addPatternPiece"), nodes,
                                   originalPointIds, nodeNamesEcho, resolveError))
    {
        return ActionResult::failure(resolveError);
    }

    QVector<QPointF> points;
    for (quint32 id : originalPointIds)
    {
        points.append(static_cast<QPointF>(*data->GeometricObject<VPointF>(id)));
    }
    if (!isSimpleClosedPolygon(points))
    {
        QJsonObject error = structuredError(QStringLiteral("invalidPiecePath"),
            QStringLiteral("\"nodes\" does not form a closed, non-self-intersecting path"));
        error["nodes"] = nodeNamesEcho;
        return ActionResult::failure(QJsonValue(error));
    }

    QString widthFormula = args.value(QStringLiteral("seamAllowanceWidth")).toString();

    try
    {
        const qreal calcWidth = qApp->toPixel(VAbstractTool::CheckFormula(0, widthFormula, data));

        VPiecePath path(PiecePathType::PiecePath);
        for (const VPieceNode &node : nodes)
        {
            path.Append(node);
        }

        VPiece piece;
        piece.SetName(name);
        piece.SetPath(path);
        piece.setSeamAllowanceWidthFormula(widthFormula, calcWidth);
        // Setting the width formula alone does not turn seam allowance ON: VAbstractPiece::
        // hasSeamAllowance() reads a separate bool (SetSeamAllowance()), the same two-step
        // "type a width" + "check the Seams checkbox" split PatternPieceDialog's own UI has.
        // Since this handler's "seamAllowanceWidth" is a required field, supplying it always
        // means seam allowance should be enabled; "seamAllowance" lets a caller override to false
        // while still recording a width formula for later use (mirrors unchecking the box without
        // clearing the field).
        piece.SetSeamAllowance(args.value(QStringLiteral("seamAllowance")).toBool(true));
        // VPiece's default-constructed fill ("") is not one of VAbstractTool::fills()' recognized
        // values; PatternPieceTool::RefreshGeometry() does fills().indexOf(piece.getFill()) and
        // feeds the result straight into static_cast<Qt::BrushStyle>(...) with no "not found" (-1)
        // guard, which crashes QBrush's constructor (reproduced and root-caused against this
        // build). PatternPieceDialog never hits this because its UI always seeds a real fill value
        // before any piece is constructed (getComboBoxCurrentData(..., FillNone) -- see
        // pattern_piece_dialog.cpp); this mirrors that same "default to FillNone" fallback.
        piece.setFill(args.value(QStringLiteral("fill")).toString(FillNone));
        if (args.contains(QStringLiteral("pieceColor")))
        {
            piece.setColor(args.value(QStringLiteral("pieceColor")).toString());
        }

        PatternPieceTool *tool = PatternPieceTool::Create(0, piece, widthFormula, pieceScene, doc, data,
                                                            Document::FullParse, Source::FromGui);
        if (tool == nullptr)
        {
            return ActionResult::failure(QStringLiteral("piece.addPatternPiece: tool creation failed for an unknown reason"));
        }

        QJsonObject payload;
        payload["id"] = static_cast<qint64>(tool->getId());
        payload["name"] = name;
        payload["op"] = QStringLiteral("piece.addPatternPiece");
        return ActionResult::success(payload);
    }
    catch (const qmu::QmuParserError &error)
    {
        QJsonObject detail = structuredError(QStringLiteral("formulaError"),
            QStringLiteral("Formula error: %1").arg(error.GetMsg()));
        detail["op"] = QStringLiteral("piece.addPatternPiece");
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
        return ActionResult::failure(QStringLiteral("piece.addPatternPiece: unknown error creating the piece"));
    }
}

// Implements "piece.addAnchorPoint": see piece_handlers.h for the documented JSON shape.
ActionResult handlePieceAddAnchorPoint(const QJsonObject &args, const ActionContext &ctx)
{
    const QString pointName = args.value(QStringLiteral("point")).toString();
    const QString pieceName = args.value(QStringLiteral("piece")).toString();
    if (pointName.isEmpty() || pieceName.isEmpty())
    {
        return ActionResult::failure(QStringLiteral("piece.addAnchorPoint requires non-empty \"point\" and \"piece\""));
    }

    VContainer *data = ctx.data();
    VAbstractPattern *doc = ctx.doc();
    if (data == nullptr || doc == nullptr)
    {
        return ActionResult::failure(QStringLiteral("piece.addAnchorPoint: context is missing a document or data container"));
    }

    const quint32 pointId = NameResolver::idForName(pointName, data, Draw::Calculation);
    const QString typeError = checkIsPoint(data, pointId, QStringLiteral("point"), pointName);
    if (!typeError.isEmpty())
    {
        return ActionResult::failure(typeError);
    }

    quint32 pieceId = 0;
    if (!resolvePieceId(pieceName, data, pieceId))
    {
        QJsonObject error = structuredError(QStringLiteral("unknownPiece"),
            QStringLiteral("Unknown piece name: %1").arg(pieceName));
        error["piece"] = pieceName;
        return ActionResult::failure(QJsonValue(error));
    }

    try
    {
        AnchorPointTool *tool = AnchorPointTool::Create(0, pointId, pieceId, doc, data, Document::FullParse,
                                                          Source::FromGui);
        if (tool == nullptr)
        {
            return ActionResult::failure(QStringLiteral("piece.addAnchorPoint: tool creation failed for an unknown reason"));
        }

        QJsonObject payload;
        payload["id"] = static_cast<qint64>(pointId);
        payload["point"] = pointName;
        payload["piece"] = pieceName;
        payload["op"] = QStringLiteral("piece.addAnchorPoint");
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
        return ActionResult::failure(QStringLiteral("piece.addAnchorPoint: unknown error adding the anchor point"));
    }
}

// Implements "piece.internalPath": see piece_handlers.h for the documented JSON shape.
ActionResult handlePieceInternalPath(const QJsonObject &args, const ActionContext &ctx)
{
    const QString pieceName = args.value(QStringLiteral("piece")).toString();
    if (pieceName.isEmpty())
    {
        return ActionResult::failure(QStringLiteral("piece.internalPath requires a non-empty \"piece\""));
    }
    const QJsonArray nodesArg = args.value(QStringLiteral("nodes")).toArray();
    if (nodesArg.size() < 2)
    {
        return ActionResult::failure(QStringLiteral("piece.internalPath requires at least 2 entries in \"nodes\""));
    }

    VContainer *data = ctx.data();
    VAbstractPattern *doc = ctx.doc();
    VMainGraphicsScene *pieceScene = ctx.pieceScene();
    if (data == nullptr || doc == nullptr || pieceScene == nullptr)
    {
        return ActionResult::failure(
            QStringLiteral("piece.internalPath: context is missing a piece scene, document, or data container"));
    }

    quint32 pieceId = 0;
    if (!resolvePieceId(pieceName, data, pieceId))
    {
        QJsonObject error = structuredError(QStringLiteral("unknownPiece"),
            QStringLiteral("Unknown piece name: %1").arg(pieceName));
        error["piece"] = pieceName;
        return ActionResult::failure(QJsonValue(error));
    }

    QVector<VPieceNode> nodes;
    QVector<quint32> originalPointIds;
    QJsonArray nodeNamesEcho;
    QString resolveError;
    if (!resolvePreparedPointNodes(nodesArg, data, doc, pieceScene, QStringLiteral("piece.internalPath"), nodes,
                                   originalPointIds, nodeNamesEcho, resolveError))
    {
        return ActionResult::failure(resolveError);
    }

    try
    {
        VPiecePath path(PiecePathType::InternalPath);
        for (const VPieceNode &node : nodes)
        {
            path.Append(node);
        }
        path.setLineColor(lineColorOrDefault(args));
        path.setLineWeight(lineWeightOrDefault(args));
        path.setCutPath(args.value(QStringLiteral("cutPath")).toBool(false));

        InternalPathTool *tool = InternalPathTool::Create(0, path, pieceId, pieceScene, doc, data,
                                                            Document::FullParse, Source::FromGui);
        if (tool == nullptr)
        {
            return ActionResult::failure(QStringLiteral("piece.internalPath: tool creation failed for an unknown reason"));
        }

        QJsonObject payload;
        payload["id"] = static_cast<qint64>(tool->getId());
        payload["piece"] = pieceName;
        payload["op"] = QStringLiteral("piece.internalPath");
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
        return ActionResult::failure(QStringLiteral("piece.internalPath: unknown error creating the internal path"));
    }
}

// Implements "piece.insertNodes": see piece_handlers.h for the documented JSON shape.
ActionResult handlePieceInsertNodes(const QJsonObject &args, const ActionContext &ctx)
{
    const QString pieceName = args.value(QStringLiteral("piece")).toString();
    if (pieceName.isEmpty())
    {
        return ActionResult::failure(QStringLiteral("piece.insertNodes requires a non-empty \"piece\""));
    }
    const QJsonArray nodesArg = args.value(QStringLiteral("nodes")).toArray();
    if (nodesArg.isEmpty())
    {
        return ActionResult::failure(QStringLiteral("piece.insertNodes requires a non-empty \"nodes\" array"));
    }

    VContainer *data = ctx.data();
    VAbstractPattern *doc = ctx.doc();
    VMainGraphicsScene *pieceScene = ctx.pieceScene();
    if (data == nullptr || doc == nullptr || pieceScene == nullptr)
    {
        return ActionResult::failure(
            QStringLiteral("piece.insertNodes: context is missing a piece scene, document, or data container"));
    }

    quint32 pieceId = 0;
    if (!resolvePieceId(pieceName, data, pieceId))
    {
        QJsonObject error = structuredError(QStringLiteral("unknownPiece"),
            QStringLiteral("Unknown piece name: %1").arg(pieceName));
        error["piece"] = pieceName;
        return ActionResult::failure(QJsonValue(error));
    }

    QVector<VPieceNode> nodes;
    QVector<quint32> nodeIds;
    QJsonArray nodeNamesEcho;
    QString resolveError;
    if (!resolvePointNodes(nodesArg, data, QStringLiteral("piece.insertNodes"), nodes, nodeIds, nodeNamesEcho,
                           resolveError))
    {
        return ActionResult::failure(resolveError);
    }

    try
    {
        // Parameter order is (nodes, pieceId, scene, data, doc) -- see this op's header comment.
        PatternPieceTool::insertNodes(nodes, pieceId, pieceScene, data, doc);

        QJsonObject payload;
        payload["piece"] = pieceName;
        payload["nodesInserted"] = nodes.size();
        payload["op"] = QStringLiteral("piece.insertNodes");
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
        return ActionResult::failure(QStringLiteral("piece.insertNodes: unknown error inserting nodes"));
    }
}

// Implements "piece.union": see piece_handlers.h for the documented JSON shape.
ActionResult handlePieceUnion(const QJsonObject &args, const ActionContext &ctx)
{
    const QString piece1Name = args.value(QStringLiteral("piece1")).toString();
    const QString piece2Name = args.value(QStringLiteral("piece2")).toString();
    if (piece1Name.isEmpty() || piece2Name.isEmpty())
    {
        return ActionResult::failure(QStringLiteral("piece.union requires non-empty \"piece1\" and \"piece2\""));
    }
    if (!args.contains(QStringLiteral("piece1EdgeIndex")) || !args.contains(QStringLiteral("piece2EdgeIndex")))
    {
        return ActionResult::failure(
            QStringLiteral("piece.union requires \"piece1EdgeIndex\" and \"piece2EdgeIndex\""));
    }

    VContainer *data = ctx.data();
    VAbstractPattern *doc = ctx.doc();
    VMainGraphicsScene *pieceScene = ctx.pieceScene();
    if (data == nullptr || doc == nullptr || pieceScene == nullptr)
    {
        return ActionResult::failure(
            QStringLiteral("piece.union: context is missing a piece scene, document, or data container"));
    }

    quint32 piece1Id = 0;
    quint32 piece2Id = 0;
    if (!resolvePieceId(piece1Name, data, piece1Id))
    {
        QJsonObject error = structuredError(QStringLiteral("unknownPiece"),
            QStringLiteral("Unknown piece name: %1").arg(piece1Name));
        error["piece"] = piece1Name;
        return ActionResult::failure(QJsonValue(error));
    }
    if (!resolvePieceId(piece2Name, data, piece2Id))
    {
        QJsonObject error = structuredError(QStringLiteral("unknownPiece"),
            QStringLiteral("Unknown piece name: %1").arg(piece2Name));
        error["piece"] = piece2Name;
        return ActionResult::failure(QJsonValue(error));
    }

    UnionToolInitData initData;
    initData.piece1_Id = piece1Id;
    initData.piece2_Id = piece2Id;
    initData.piece1_Index = static_cast<quint32>(args.value(QStringLiteral("piece1EdgeIndex")).toInt());
    initData.piece2_Index = static_cast<quint32>(args.value(QStringLiteral("piece2EdgeIndex")).toInt());
    initData.scene = pieceScene;
    initData.doc = doc;
    initData.data = data;
    initData.parse = Document::FullParse;
    initData.typeCreation = Source::FromGui;
    initData.retainPieces = args.value(QStringLiteral("retainPieces")).toBool(false);

    try
    {
        UnionTool *tool = UnionTool::Create(0, initData);
        if (tool == nullptr)
        {
            return ActionResult::failure(QStringLiteral("piece.union: tool creation failed for an unknown reason"));
        }

        QJsonObject payload;
        payload["id"] = static_cast<qint64>(tool->getId());
        payload["piece1"] = piece1Name;
        payload["piece2"] = piece2Name;
        payload["op"] = QStringLiteral("piece.union");
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
        return ActionResult::failure(QStringLiteral("piece.union: unknown error uniting the pieces"));
    }
}
