//---------------------------------------------------------------------------------------------------------------------
//  @file   piece_dump_handler.cpp
//  @author Seamly2D Contributors
//  @date   22 Aug, 2026
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

#include "piece_dump_handler.h" // Brings in the ActionResult-returning handlePieceDump declaration this file implements.

#include "../action_context.h" // Brings in ActionContext, supplying the data container this handler reads.
#include "pattern_dump_handler.h" // Brings in toolToString(), reused here for each node's "type" field instead of a second, potentially-drifting stringification of the same Tool enum.

#include "../../vpatterndb/vcontainer.h"  // Brings in VContainer::DataPieces()/GetGObject()/getPiecePath(), the sources this handler reads from.
#include "../../vpatterndb/vpiece.h"      // Brings in VPiece: GetName(), GetPath(), hasSeamAllowance(), getSeamAllowanceWidthFormula(), getInternalPaths(), getAnchors().
#include "../../vpatterndb/vpiecepath.h"  // Brings in VPiecePath: nodeCount(), at(), isCutPath().
#include "../../vpatterndb/vpiecenode.h"  // Brings in VPieceNode: GetId(), GetTypeTool(), GetReverse().
#include "../../vgeometry/vgobject.h"     // Brings in VGObject: name(), getType().
#include "../../vgeometry/vpointf.h"      // Brings in VPointF: x(), y(), used only for GOType::Point objects.
#include "../../vgeometry/vgeometrydef.h" // Brings in the GOType enum, compared against below.
#include "../../vmisc/def.h"              // Brings in the Tool enum, compared against below.
#include "../../ifc/exception/vexception.h" // Brings in VExceptionBadId, thrown by GetGObject()/getPiecePath() for an id with nothing registered at it.

#include <QJsonArray>     // Provides QJsonArray, used for "nodes"/"internalPaths"/"anchors".
#include <QJsonObject>    // Provides QJsonObject, used for each per-node/per-path/per-anchor JSON record.
#include <QJsonValue>     // Provides QJsonValue, the "piece" argument's own type (string or number).
#include <QSharedPointer> // Provides QSharedPointer and qSharedPointerDynamicCast, used to reach VPointF-specific data.

namespace
{
    // Resolves the "piece" argument (a JSON string name or JSON number id) to a piece id. See
    // render_handlers.cpp's own resolvePieceRefArg() -- duplicated here rather than shared, matching
    // this codebase's existing convention of a small, file-local helper per handler file instead of
    // a cross-file utility header (see e.g. structuredError(), duplicated verbatim across nine of
    // this directory's handler files). Returns false (leaving outId untouched) if neither resolves
    // to a known piece.
    bool resolvePieceRefArg(const QJsonValue &pieceArg, const VContainer *data, quint32 &outId)
    {
        const QHash<quint32, VPiece> *pieces = data->DataPieces();
        if (pieces == nullptr)
        {
            return false;
        }
        if (pieceArg.isDouble()) // A numeric "piece" value is taken as a literal piece id.
        {
            const quint32 id = static_cast<quint32>(pieceArg.toDouble());
            if (pieces->contains(id))
            {
                outId = id;
                return true;
            }
            return false;
        }
        const QString name = pieceArg.toString(); // Otherwise treated as a piece name.
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

    // Builds one node's JSON record: {"id","type","reverse","name"?,"x"?,"y"?,"unsupported"?}. See
    // piece_dump_handler.h's own doc comment for the exact contract this implements -- in
    // particular, GetGObject() throwing VExceptionBadId (an id with nothing registered at it, rather
    // than a null/zero id -- there is no such sentinel here) is caught, not propagated: this handler
    // never crashes or silently drops a node it cannot fully resolve, it always reports what it has
    // (id/type/reverse at minimum) with "unsupported" set instead.
    QJsonObject dumpNode(const VPieceNode &node, const VContainer *data)
    {
        QJsonObject entry;
        const quint32 nodeId = node.GetId();
        entry["id"] = static_cast<qint64>(nodeId);
        entry["type"] = toolToString(node.GetTypeTool());
        entry["reverse"] = node.GetReverse();

        // Only Tool::NodePoint is constructible via this action layer's piece.addPatternPiece/
        // piece.internalPath "nodes" arrays today (see piece_handlers.h's own documented gap
        // comment on arc/curve piece nodes) -- flagged here regardless of whether the underlying
        // object below resolves cleanly, since round-tripping a non-point node back through this
        // action layer's own creation ops is not supported either way.
        bool unsupported = (node.GetTypeTool() != Tool::NodePoint);

        try
        {
            const QSharedPointer<VGObject> obj = data->GetGObject(nodeId);
            if (!obj.isNull())
            {
                entry["name"] = obj->name();
                if (obj->getType() == GOType::Point)
                {
                    const QSharedPointer<VPointF> point = qSharedPointerDynamicCast<VPointF>(obj);
                    if (!point.isNull())
                    {
                        entry["x"] = point->x();
                        entry["y"] = point->y();
                    }
                }
            }
        }
        catch (const VExceptionBadId &)
        {
            unsupported = true; // Nothing registered at this node's own id at all; still reported, just flagged.
        }

        if (unsupported)
        {
            entry["unsupported"] = true;
        }
        return entry;
    }

    // Builds one anchor point's JSON record: {"id","name"?,"x"?,"y"?}. Anchors (VPiece::getAnchors())
    // are plain point-clone ids (see AnchorPointTool::Create(), anchorpoint_tool.cpp), not wrapped in
    // a VPieceNode the way main-path/internal-path nodes are -- no "type"/"reverse"/"unsupported" to
    // report, but the same "never crash on an unresolvable id" contract applies.
    QJsonObject dumpAnchor(quint32 anchorId, const VContainer *data)
    {
        QJsonObject entry;
        entry["id"] = static_cast<qint64>(anchorId);
        try
        {
            const QSharedPointer<VGObject> obj = data->GetGObject(anchorId);
            if (!obj.isNull())
            {
                entry["name"] = obj->name();
                if (obj->getType() == GOType::Point)
                {
                    const QSharedPointer<VPointF> point = qSharedPointerDynamicCast<VPointF>(obj);
                    if (!point.isNull())
                    {
                        entry["x"] = point->x();
                        entry["y"] = point->y();
                    }
                }
            }
        }
        catch (const VExceptionBadId &)
        {
            entry["unsupported"] = true;
        }
        return entry;
    }
}

// Implements "piece.dump": see piece_dump_handler.h for the documented JSON shape.
ActionResult handlePieceDump(const QJsonObject &args, const ActionContext &ctx)
{
    if (!args.contains(QStringLiteral("piece")))
    {
        return ActionResult::failure(QStringLiteral("piece.dump requires a \"piece\" (name or id)"));
    }

    const VContainer *data = ctx.data();
    if (data == nullptr)
    {
        return ActionResult::failure(QStringLiteral("piece.dump: context is missing a data container"));
    }

    quint32 pieceId = 0;
    if (!resolvePieceRefArg(args.value(QStringLiteral("piece")), data, pieceId))
    {
        QJsonObject error;
        error["type"] = QStringLiteral("unknownPiece");
        error["message"] = QStringLiteral("Unknown piece: %1").arg(args.value(QStringLiteral("piece")).toVariant().toString());
        error["piece"] = args.value(QStringLiteral("piece"));
        return ActionResult::failure(QJsonValue(error));
    }

    const VPiece piece = data->DataPieces()->value(pieceId); // Presence already confirmed by resolvePieceRefArg() above.

    QJsonObject payload;
    payload["id"] = static_cast<qint64>(pieceId);
    payload["name"] = piece.GetName();
    payload["seamAllowance"] = piece.hasSeamAllowance();
    payload["seamAllowanceWidthFormula"] = piece.getSeamAllowanceWidthFormula();

    QJsonArray nodes; // The piece's main outline path (the same one piece.addPatternPiece's "nodes" array built).
    const VPiecePath mainPath = piece.GetPath();
    for (int i = 0; i < mainPath.nodeCount(); ++i)
    {
        nodes.append(dumpNode(mainPath.at(i), data));
    }
    payload["nodes"] = nodes;

    QJsonArray internalPaths; // Every internal path added via piece.internalPath, each as {"id","cutPath","nodes"} or {"id","unsupported":true}.
    const QVector<quint32> internalPathIds = piece.getInternalPaths();
    for (quint32 pathId : internalPathIds)
    {
        QJsonObject pathEntry;
        pathEntry["id"] = static_cast<qint64>(pathId);
        try
        {
            const VPiecePath path = data->getPiecePath(pathId);
            pathEntry["cutPath"] = path.isCutPath();
            QJsonArray pathNodes;
            for (int i = 0; i < path.nodeCount(); ++i)
            {
                pathNodes.append(dumpNode(path.at(i), data));
            }
            pathEntry["nodes"] = pathNodes;
        }
        catch (const VExceptionBadId &)
        {
            pathEntry["unsupported"] = true; // Nothing registered at this internal-path id at all; still reported, just flagged.
        }
        internalPaths.append(pathEntry);
    }
    payload["internalPaths"] = internalPaths;

    QJsonArray anchors; // Every anchor point added via piece.addAnchorPoint.
    const QVector<quint32> anchorIds = piece.getAnchors();
    for (quint32 anchorId : anchorIds)
    {
        anchors.append(dumpAnchor(anchorId, data));
    }
    payload["anchors"] = anchors;

    payload["op"] = QStringLiteral("piece.dump");
    return ActionResult::success(payload);
}
