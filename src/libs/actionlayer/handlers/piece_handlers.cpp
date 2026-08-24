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
#include "../piece_layout_cursor.h" // Brings in PieceLayoutCursor, consulted by handlePieceAddPatternPiece() below when "mx"/"my" are both omitted.
#include "operation_handlers.h"     // Brings in handleGroup(), reused directly by handlePieceAddPatternPiece()'s "createGroup" option instead of reimplementing its DOM-bookkeeping logic.

#include "../../vpatterndb/vcontainer.h"
#include "../../vpatterndb/vpiece.h"
#include "../../vpatterndb/vpiecenode.h"
#include "../../vpatterndb/vpiecepath.h"
#include "../../vgeometry/vgobject.h"
#include "../../vgeometry/vgeometrydef.h"
#include "../../vgeometry/vpointf.h"
#include "../../ifc/xml/vabstractpattern.h"
#include "../../ifc/xml/vtoolrecord.h" // Brings in VToolRecord: getId()/getDraftBlockName(), read by checkSameDraftBlock() below to trace which draft block created a given object id.
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
#include <QPolygonF>
#include <QRectF>
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

    // Returns the draft block name doc->getHistory() recorded for objectId -- via
    // VToolRecord::getDraftBlockName(), the same field VAbstractTool::AddRecord() stamps every
    // tool's own id with, using whatever doc->getActiveDraftBlockName() was AT THE TIME that
    // specific tool was created (see vabstracttool.cpp's AddRecord()) -- or an empty string if no
    // history entry names this id at all.
    //
    // KNOWN GAP (documented, not silently unhandled): an operation-created destination point
    // (move/rotation/mirrorByLine/mirrorByAxis) has no individual history entry of its own -- only
    // the *operation* tool's own id does (confirmed by reading vtoolmove.cpp/vtoolrotation.cpp/
    // vtoolmirrorbyline.cpp/vtoolmirrorbyaxis.cpp's own AddRecord() calls, each passing the
    // operation's own id, never a per-destination-point one). Such a point's own draft block
    // cannot be determined this way; checkSameDraftBlock() below treats that as "unverifiable,"
    // not as a violation, rather than either false-blocking a legitimate reference or attempting a
    // more complex reverse trace back to an owning operation tool. Flagged as a real, narrow,
    // currently-uncovered gap in this cross-draft-block check, not something this fix claims to
    // close completely.
    QString draftBlockForObjectId(quint32 objectId, VAbstractPattern *doc)
    {
        QVector<VToolRecord> *history = doc->getHistory();
        if (history != nullptr)
        {
            for (const VToolRecord &record : *history)
            {
                if (record.getId() == objectId)
                {
                    return record.getDraftBlockName();
                }
            }
        }
        return QString();
    }

    // FOUND AND FIXED -- cross-draft-block dangling reference (see piece_handlers.h's own
    // module-level comment for the full root-cause writeup): confirms every id in ids was created
    // in the same draft block as doc's own currently-active one
    // (VAbstractPattern::getActiveDraftBlockName()). A mismatch here would otherwise silently
    // corrupt the saved file on reload -- not in this live session, where VContainer's
    // calculation-scope objects never get wiped mid-session, only during file *parsing*
    // (VPattern::parseDraftBlockElement()'s ClearCalculationGObjects() call) -- so this check has
    // to run BEFORE any DOM element or modeling clone is created from these ids, not after.
    //
    // An id with no history entry at all (see draftBlockForObjectId()'s own comment on the one
    // documented gap this leaves) is skipped -- treated as unverifiable, not as a violation.
    bool checkSameDraftBlock(const QVector<quint32> &ids, const QJsonArray &namesEcho, VAbstractPattern *doc,
                             QJsonValue &outError)
    {
        const QString activeDraftBlock = doc->getActiveDraftBlockName();
        QJsonArray offendingNodes;
        QString foundDraftBlock;
        for (int i = 0; i < ids.size(); ++i)
        {
            const QString block = draftBlockForObjectId(ids.at(i), doc);
            if (block.isEmpty() || block == activeDraftBlock)
            {
                continue; // Either unverifiable (see above) or already matches -- nothing to report.
            }
            offendingNodes.append(namesEcho.at(i));
            foundDraftBlock = block; // Last mismatching block found; sufficient for a single-field error report -- a script referencing more than one *other* draft block in one call is already unusual enough that naming just one is a fine diagnostic starting point.
        }
        if (offendingNodes.isEmpty())
        {
            return true;
        }

        QJsonObject error;
        error["type"] = QStringLiteral("crossDraftBlockReference");
        error["message"] = QStringLiteral(
            "One or more referenced nodes belong to a different draft block ('%1') than the "
            "currently active one ('%2') -- referencing a point from another draft block would "
            "silently corrupt this pattern on reload, since that other draft block's own "
            "calculation-scope objects get cleared once a later block's own <calculation> "
            "section is parsed.").arg(foundDraftBlock, activeDraftBlock);
        error["offendingNodes"] = offendingNodes;
        error["expectedDraftBlock"] = activeDraftBlock;
        error["foundDraftBlock"] = foundDraftBlock;
        outError = QJsonValue(error);
        return false;
    }

    // Resolves a JSON array of point names into a QVector<VPieceNode> (every node Tool::NodePoint,
    // reverse=false -- see this file's header comment on the documented arc/curve-node gap).
    // Returns an empty vector (and fills outError) on the first unresolved/wrong-type name, or on
    // a cross-draft-block reference (checkSameDraftBlock() above) once every name has resolved.
    bool resolvePointNodes(const QJsonArray &namesArg, const VContainer *data, VAbstractPattern *doc,
                           const QString &op, QVector<VPieceNode> &outNodes, QVector<quint32> &outIds,
                           QJsonArray &outNamesEcho, QJsonValue &outError)
    {
        for (const QJsonValue &nameValue : namesArg)
        {
            const QString name = nameValue.toString();
            if (name.isEmpty())
            {
                outError = QJsonValue(QStringLiteral("%1: \"nodes\" contains a non-string/empty entry").arg(op));
                return false;
            }
            const quint32 id = NameResolver::idForName(name, data, Draw::Calculation); // Uncaught by design; ActionEngine's ActionResolverError clause reports it.
            const QString typeError = checkIsPoint(data, id, QStringLiteral("nodes"), name);
            if (!typeError.isEmpty())
            {
                outError = QJsonValue(typeError);
                return false;
            }
            outNodes.append(VPieceNode(id, Tool::NodePoint, false));
            outIds.append(id);
            outNamesEcho.append(name);
        }
        if (!checkSameDraftBlock(outIds, outNamesEcho, doc, outError))
        {
            return false;
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
    // resolvePointNodes() (above) intentionally passes the original point ids straight through.
    //
    // Resolves every name (Phase 1: name -> id, type-checked) and validates the whole set belongs
    // to the currently active draft block (Phase 2: checkSameDraftBlock()) BEFORE creating a
    // single clone (Phase 3) -- deliberately split into these three passes, rather than resolving
    // and cloning one name at a time in a single loop as an earlier version of this function did,
    // so that ANY failure (an unresolved name, a cross-draft-block reference) leaves zero dangling,
    // unreferenced modeling clones behind. A failure partway through a single combined loop would
    // have already cloned every name processed so far with no way to undo it -- exactly the kind
    // of non-atomic failure piece_handlers.h's own "FOUND AND FIXED -- cross-draft-block..."
    // module comment warns a caller's automated retry logic could stumble over.
    bool resolvePreparedPointNodes(const QJsonArray &namesArg, VContainer *data, VAbstractPattern *doc,
                                   VMainGraphicsScene *scene, const QString &op, QVector<VPieceNode> &outNodes,
                                   QVector<quint32> &outOriginalIds, QJsonArray &outNamesEcho, QJsonValue &outError)
    {
        // Phase 1: name -> id, type-checked. No clone created yet.
        QVector<quint32> originalIds;
        for (const QJsonValue &nameValue : namesArg)
        {
            const QString name = nameValue.toString();
            if (name.isEmpty())
            {
                outError = QJsonValue(QStringLiteral("%1: \"nodes\" contains a non-string/empty entry").arg(op));
                return false;
            }
            const quint32 pointId = NameResolver::idForName(name, data, Draw::Calculation); // Uncaught by design; ActionEngine's ActionResolverError clause reports it.
            const QString typeError = checkIsPoint(data, pointId, QStringLiteral("nodes"), name);
            if (!typeError.isEmpty())
            {
                outError = QJsonValue(typeError);
                return false;
            }
            originalIds.append(pointId);
            outNamesEcho.append(name);
        }

        // Phase 2: every resolved id must belong to the pattern's currently active draft block.
        if (!checkSameDraftBlock(originalIds, outNamesEcho, doc, outError))
        {
            return false;
        }

        // Phase 3: only now, with every id validated, actually clone each one.
        for (quint32 pointId : originalIds)
        {
            const quint32 nodeId = VAbstractTool::CreateNode<VPointF>(data, pointId);
            // Defense-in-depth, kept intentionally even though NameResolver::idForName()'s scoped
            // (Draw::Calculation) overload -- used everywhere in this action layer that resolves a
            // calculation-context name, including the lookup above -- now makes this rename
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
    QJsonValue resolveError;
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

    // "mx"/"my": PatternPieceTool::RefreshGeometry() (pattern_piece_tool.cpp:1586) applies
    // setPos(piece.GetMx(), piece.GetMy()) on top of the item's own node-derived local shape --
    // VPiece default-constructs both to 0, and neither this handler nor PatternPieceTool::Create()
    // itself (confirmed by reading it) ever assigns anything else, so every piece built with no
    // explicit "mx"/"my" used to land at the exact same position: reproduced directly (20 Aug 2026)
    // -- two pieces built from two independently-drafted, coordinate-overlapping draft blocks
    // rendered completely on top of each other. Auto-placement (PieceLayoutCursor, only consulted
    // when BOTH "mx" and "my" are omitted -- an explicit value, even just one of the two, always
    // wins outright and is never adjusted) exists specifically to give a headless/AI-only caller
    // the same "pieces don't overlap by default" outcome a human gets for free by dragging each
    // new piece to a clear spot after creating it.
    qreal mx = 0.0;
    qreal my = 0.0;
    if (args.contains(QStringLiteral("mx")) || args.contains(QStringLiteral("my")))
    {
        mx = args.value(QStringLiteral("mx")).toDouble(0.0);
        my = args.value(QStringLiteral("my")).toDouble(0.0);
    }
    else if (PieceLayoutCursor *layoutCursor = ctx.pieceLayoutCursor())
    {
        const QRectF rawBounds = QPolygonF(points).boundingRect();
        const QPointF offset = layoutCursor->placeNext(rawBounds);
        mx = offset.x();
        my = offset.y();
    }
    // No cursor available at all (e.g. a raw ActionLayerTest fixture with no PatternSession behind
    // it) and no explicit "mx"/"my": falls through with mx=my=0.0, exactly matching this op's
    // pre-existing behavior for every such context -- see ActionContext::pieceLayoutCursor()'s own
    // doc comment on why this is the correct, non-breaking fallback rather than a hard error.

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
        piece.SetMx(mx);
        piece.SetMy(my);

        PatternPieceTool *tool = PatternPieceTool::Create(0, piece, widthFormula, pieceScene, doc, data,
                                                            Document::FullParse, Source::FromGui);
        if (tool == nullptr)
        {
            return ActionResult::failure(QStringLiteral("piece.addPatternPiece: tool creation failed for an unknown reason"));
        }

        QJsonObject payload;
        payload["id"] = static_cast<qint64>(tool->getId());
        payload["name"] = name;
        payload["mx"] = mx; // The position actually used -- either the caller's own explicit "mx"/"my", or (both omitted) whatever PieceLayoutCursor auto-placed this piece at, so a caller can see the resolved position either way without re-deriving it.
        payload["my"] = my;
        payload["op"] = QStringLiteral("piece.addPatternPiece");

        // "createGroup" (optional, default true -- see this op's own schema description in
        // action_registry.cpp for why the default is on): unlike the interactive GUI, where a
        // human separately builds groups by hand via the Group Manager panel's own "+" button
        // (PatternPieceTool::Create() itself never creates one -- confirmed true of the real,
        // unmodified GUI, not just this action layer), the action engine has no human in the loop
        // to perform that step, so a multi-piece pattern built entirely through this action layer
        // would otherwise always have an empty Group Manager, unlike any comparable human-authored
        // file. Defaulting this on gives every AI-assembled piece the same "one group per piece"
        // organizational structure a human author typically builds manually (see e.g. the Aldrich
        // fixture's own piece/group naming symmetry). Reuses handleGroup() (operation_
        // handlers.cpp/.h) directly rather than reimplementing its DOM-bookkeeping logic, exactly
        // the way that handler itself already reuses AddGroup::redo()'s (vtools/undocommands/
        // addgroup.cpp) non-undo-stack logic. Grouped by the ORIGINAL draft point names
        // (nodeNamesEcho, e.g. "A"/"B"/"C"/"D") -- not this piece's own internal
        // "__pieceNode_<id>"-named clones (see resolvePreparedPointNodes()'s own comment on that
        // rename) -- since those clones are purely an internal implementation detail, never meant
        // to be independently selected/grouped, and NameResolver's Draw::Calculation scoping
        // (which handleGroup()'s own "sourceObjects" resolution uses) could not resolve a clone's
        // mangled name by name anyway. The piece itself has already been created successfully by
        // this point, so a group-creation failure (e.g. a name collision with an unrelated
        // pre-existing group) is reported alongside the success payload as "groupError" rather
        // than failing this whole action -- the piece really does exist either way, and reporting
        // this action as failed would misleadingly suggest otherwise.
        if (args.value(QStringLiteral("createGroup")).toBool(true))
        {
            const QString groupName = args.value(QStringLiteral("groupName")).toString(name);
            QJsonObject groupArgs;
            groupArgs["name"] = groupName;
            groupArgs["sourceObjects"] = nodeNamesEcho;
            const ActionResult groupResult = handleGroup(groupArgs, ctx);
            if (groupResult.ok)
            {
                payload["group"] = groupResult.value;
            }
            else
            {
                payload["groupError"] = groupResult.error;
            }
        }

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

    // See piece_handlers.h's own "FOUND AND FIXED -- cross-draft-block..." module comment: AnchorPointTool::
    // Create() clones "point" (CreateNode<VPointF>()) the same way piece.addPatternPiece/
    // piece.internalPath's own node resolution does, so an anchor referencing a point from a
    // different draft block than the one currently active is exposed to the exact same
    // silently-corrupts-on-reload failure mode -- checked here, before Create() (and its internal
    // clone) ever runs, for the same reason.
    QJsonValue blockError;
    if (!checkSameDraftBlock(QVector<quint32>{pointId}, QJsonArray{pointName}, doc, blockError))
    {
        return ActionResult::failure(blockError);
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
    QJsonValue resolveError;
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
    QJsonValue resolveError;
    if (!resolvePointNodes(nodesArg, data, doc, QStringLiteral("piece.insertNodes"), nodes, nodeIds, nodeNamesEcho,
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
