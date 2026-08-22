//---------------------------------------------------------------------------------------------------------------------
//  @file   history_undo_handlers.cpp
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

#include "history_undo_handlers.h" // Brings in the ActionResult-returning handlePatternUndo declaration this file implements.

#include "../action_context.h" // Brings in ActionContext, supplying doc/data for this handler.

#include "../../ifc/xml/vabstractpattern.h" // Brings in VAbstractPattern: getHistory(), changeActiveDraftBlock().
#include "../../ifc/xml/vtoolrecord.h"       // Brings in VToolRecord: getId()/getTypeTool()/getDraftBlockName().
#include "../../ifc/exception/vexception.h"  // Brings in VException (and VExceptionBadId), thrown by VContainer::GetPiece() for a stale id.
#include "../../vpatterndb/vcontainer.h"     // Brings in VContainer: GetPiece(), used to build DeletePiece's required VPiece value.
#include "../../vpatterndb/vpiece.h"         // Brings in VPiece, DeletePiece's third constructor argument.
#include "../../vmisc/def.h"                 // Brings in the Tool enum, switched on below to classify each history entry.

// actionlayer.pro does not link libvtools itself -- only actiond.pro/ActionLayerTest.pro do, for
// the final executable -- so these are header-only dependencies, the same idiom point_edit_
// handlers.cpp already relies on for vdatatool.h/vtoolbasepoint.h/vtoolendline.h.
#include "../../vtools/undocommands/vundocommand.h"      // Brings in VUndoCommand, the common base every delete-command below derives from.
#include "../../vtools/undocommands/deltool.h"           // Brings in DelTool, reversing every history entry except BasePoint/Piece (see this file's own header comment).
#include "../../vtools/undocommands/deletepiece.h"       // Brings in DeletePiece, reversing a Tool::Piece entry.
#include "../../vtools/undocommands/delete_draftblock.h" // Brings in DeleteDraftBlock, reversing a Tool::BasePoint entry.

#include <QJsonArray>     // Provides QJsonArray, the "entries" field of this op's success payload.
#include <QJsonObject>    // Provides QJsonObject, used for both the input args and the output payload.
#include <QScopedPointer> // Provides QScopedPointer, owning each constructed delete-command for exactly one redo() call.
#include <QString>        // Provides QString, used for the "kind" field and structured-error messages.
#include <QVector>         // Provides QVector, the type doc->getHistory() returns a pointer to.

namespace
{
    QJsonObject structuredError(const QString &type, const QString &message)
    {
        QJsonObject error;
        error["type"] = type;
        error["message"] = message;
        return error;
    }

    // Constructs, and immediately runs, the one delete-command class that correctly reverses the
    // given history record -- see history_undo_handlers.h's own header comment for the full
    // reasoning behind this three-way split (verified against every deleteTool()/Remove()
    // override that actually pushes a delete-command in the current tree). Returns the "kind"
    // string this entry's JSON record reports; throws VException (VExceptionBadId in particular,
    // from VContainer::GetPiece()) if the record refers to an id that's already gone -- which
    // should never happen given this handler's own strict reverse-chronological ordering, but is
    // not silently swallowed if it somehow does.
    QString reverseOneHistoryEntry(VAbstractPattern *doc, VContainer *data, const VToolRecord &record)
    {
        // Every delete-command's own constructor captures doc->getActiveDraftBlockName() (DelTool,
        // DeleteDraftBlock) or otherwise assumes it -- matching AddToCalc::undo()/DelTool::redo()'s
        // own "doc->changeActiveDraftBlock(...)/setCurrentDraftBlock(...) first, always" convention
        // (vundocommand.cpp/addtocalc.cpp/deltool.cpp) -- so this must run before constructing the
        // command below, not after, and for every kind (not only DeleteDraftBlock's own case),
        // since a pattern with more than one draft block (e.g. "Front" and "Back" in the same
        // file) must have each entry reversed against ITS OWN block, not whichever was last active.
        doc->changeActiveDraftBlock(record.getDraftBlockName());

        const quint32 id = record.getId();
        const Tool toolType = record.getTypeTool();

        QScopedPointer<VUndoCommand> command;
        QString kind;
        if (toolType == Tool::BasePoint)
        {
            // A basePoint action creates a whole new <draftBlock>, not just one <point> -- DelTool
            // alone would remove only the point, leaving an orphaned empty draft block behind (see
            // VToolBasePoint::deleteTool(), vtoolbasepoint.cpp, for the real override this mirrors).
            command.reset(new DeleteDraftBlock(doc, record.getDraftBlockName()));
            kind = QStringLiteral("draftBlock");
        }
        else if (toolType == Tool::Piece)
        {
            // DeletePiece's constructor needs the piece's own current VPiece value, not just its
            // id (see PatternPieceTool::deleteTool(), pattern_piece_tool.cpp, for the real override
            // this mirrors) -- GetPiece() throws VExceptionBadId if the id is already gone, which
            // this function deliberately does not catch: see this function's own doc comment.
            const VPiece piece = data->GetPiece(id);
            command.reset(new DeletePiece(doc, id, piece));
            kind = QStringLiteral("piece");
        }
        else
        {
            // Every other Tool enumerator this action layer can create -- points, lines, curves,
            // cut-points, operations, and modeling-scope entries like InternalPath/AnchorPoint/
            // NodePoint -- uses the base VAbstractTool::deleteTool()'s own command (vabstracttool.cpp).
            command.reset(new DelTool(doc, id));
            kind = QStringLiteral("tool");
        }

        command->redo(); // Removes the DOM element; see history_undo_handlers.h's own comment on why this alone doesn't yet prune VContainer.
        return kind;
    }
}

// Implements "pattern.undo": see history_undo_handlers.h for the documented JSON shape, the full
// design rationale (including why the abandoned QUndoStack-based design was replaced), and the
// exact split of responsibility between this handler and PatternSession::runActions().
ActionResult handlePatternUndo(const QJsonObject &args, const ActionContext &ctx)
{
    const int requestedCount = args.contains(QStringLiteral("count")) ? args.value(QStringLiteral("count")).toInt(1) : 1;
    if (requestedCount < 0)
    {
        return ActionResult::failure(QStringLiteral("pattern.undo: \"count\" must not be negative"));
    }

    VAbstractPattern *doc = ctx.doc();
    VContainer *data = ctx.data();
    if (doc == nullptr || data == nullptr)
    {
        return ActionResult::failure(QStringLiteral("pattern.undo: context is missing a document or data container"));
    }

    QVector<VToolRecord> *history = doc->getHistory(); // Mutable pointer into VAbstractPattern's own m_history -- see the loop below for why this handler prunes it directly as it goes.
    if (history == nullptr) // getHistory() can return nullptr before a document has been parsed (pattern_dump_handler.cpp's own pattern.dump guards the same way).
    {
        QJsonObject payload;
        payload["undone"] = 0;
        payload["remaining"] = 0;
        payload["entries"] = QJsonArray();
        return ActionResult::success(payload);
    }

    const int toUndo = qMin(requestedCount, history->size()); // Never more than what's actually available -- see this op's own header comment on why running out is a clean partial result, not an error.

    QJsonArray entries; // Accumulates {"id","kind"} for each entry actually removed, most-recent first.
    int undone = 0;
    try
    {
        for (int i = 0; i < toUndo; ++i)
        {
            // Re-reads history->last() fresh each iteration (rather than snapshotting indices up
            // front) so this loop stays correct even if reverseOneHistoryEntry() ever needed to
            // remove more than one record for a single entry in the future -- it doesn't today,
            // but this avoids an index trap if that ever changes. history->removeLast() below is
            // load-bearing, not cosmetic: nothing else prunes m_history mid-batch (a full DOM-driven
            // rebuild only happens once, in PatternSession::runActions(), after this whole handler
            // returns -- see history_undo_handlers.h's own comment) -- without it, the next
            // iteration would re-read the SAME already-DOM-removed record and either loop on it
            // forever or throw trying to reverse an id that's already gone.
            const VToolRecord record = history->last();
            const QString kind = reverseOneHistoryEntry(doc, data, record);
            history->removeLast();

            QJsonObject entry;
            entry["id"] = static_cast<qint64>(record.getId());
            entry["kind"] = kind;
            entries.append(entry);
            ++undone;
        }
    }
    catch (const VException &error) // VExceptionBadId in particular, from VContainer::GetPiece() -- see reverseOneHistoryEntry()'s own comment on why this isn't expected in normal use.
    {
        QJsonObject detail = structuredError(QStringLiteral("undoInconsistency"),
            QStringLiteral("pattern.undo: history is inconsistent with live pattern state: %1").arg(error.ErrorMessage()));
        detail["undoneBeforeFailure"] = undone; // However many entries were successfully removed before this one failed -- not silently discarded.
        detail["entries"] = entries;
        return ActionResult::failure(QJsonValue(detail));
    }
    catch (const std::exception &error)
    {
        return ActionResult::failure(QString::fromUtf8(error.what()));
    }
    catch (...)
    {
        return ActionResult::failure(QStringLiteral("pattern.undo: unknown error reversing pattern history"));
    }

    QJsonObject payload;
    payload["undone"] = undone;               // How many entries this call actually removed.
    payload["remaining"] = history->size();    // How many history entries are left after this call.
    payload["entries"] = entries;              // What was removed, most-recent first.
    return ActionResult::success(payload);
}
