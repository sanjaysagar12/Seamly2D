//---------------------------------------------------------------------------------------------------------------------
//  @file   session_undo_handlers.cpp
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

#include "session_undo_handlers.h" // Brings in the ActionResult-returning handleSessionUndo/Redo/UndoStatus declarations this file implements.

#include "../action_context.h" // Brings in ActionContext; unused by every handler's own body (see session_undo_handlers.h's header comment) but required by the shared signature.

#include "../../vmisc/vabstractapplication.h" // Brings in the qApp macro, used for qApp->getUndoStack().

#include <QJsonArray>  // Provides QJsonArray, the "labels" field of session.undoStatus's payload.
#include <QJsonObject> // Provides QJsonObject, used for both the input args and every output payload.
#include <QUndoStack>  // Provides QUndoStack, the type every handler below operates on directly.
#include <algorithm>   // Provides std::max/std::min, used to clamp session.undoStatus's label window to the stack's actual bounds.

namespace
{
    // Shared {"...Count","canUndo","canRedo","index","count"} payload shape both session.undo and
    // session.redo build, differing only in which counting field name they use ("undone"/"redone")
    // and which of undo()/canUndo() vs redo()/canRedo() the caller already ran. actualCount is how
    // many steps this call actually performed (bounded by how many were left -- see this file's
    // header comment on why running out early is success, not failure).
    QJsonObject buildStepResultPayload(const QString &countField, int actualCount, const QUndoStack *stack)
    {
        QJsonObject payload;
        payload[countField] = actualCount;
        payload[QStringLiteral("canUndo")] = stack->canUndo();
        payload[QStringLiteral("canRedo")] = stack->canRedo();
        payload[QStringLiteral("index")] = stack->index();
        payload[QStringLiteral("count")] = stack->count();
        return payload;
    }

    // Reads and validates the optional "count" argument shared by session.undo/session.redo:
    // defaults to 1 when absent, and is rejected as a structured failure only if explicitly
    // negative (zero is a valid, if pointless, request -- "undo zero times" is a legitimate no-op,
    // not an error). Returns false (leaving outCount untouched) on a negative "count".
    bool readStepCount(const QJsonObject &args, const QString &op, int &outCount, ActionResult &outError)
    {
        const int requested = args.contains(QStringLiteral("count")) ? args.value(QStringLiteral("count")).toInt(1) : 1;
        if (requested < 0)
        {
            outError = ActionResult::failure(QStringLiteral("%1: \"count\" must not be negative").arg(op));
            return false;
        }
        outCount = requested;
        return true;
    }
}

// Implements "session.undo": see session_undo_handlers.h for the documented JSON shape.
ActionResult handleSessionUndo(const QJsonObject &args, const ActionContext &ctx)
{
    Q_UNUSED(ctx) // qApp->getUndoStack() is qApp-scoped, not ActionContext-scoped -- see this file's own header comment.

    int requestedCount = 0;
    ActionResult countError;
    if (!readStepCount(args, QStringLiteral("session.undo"), requestedCount, countError))
    {
        return countError;
    }

    QUndoStack *stack = qApp->getUndoStack();
    int undone = 0;
    for (int i = 0; i < requestedCount && stack->canUndo(); ++i)
    {
        stack->undo(); // Runs the one QUndoCommand (or macro) at index-1's own undo(); see AddToCalc::undo() etc. for what that actually rewrites.
        ++undone;
    }

    return ActionResult::success(buildStepResultPayload(QStringLiteral("undone"), undone, stack));
}

// Implements "session.redo": see session_undo_handlers.h for the documented JSON shape.
ActionResult handleSessionRedo(const QJsonObject &args, const ActionContext &ctx)
{
    Q_UNUSED(ctx) // qApp->getUndoStack() is qApp-scoped, not ActionContext-scoped -- see this file's own header comment.

    int requestedCount = 0;
    ActionResult countError;
    if (!readStepCount(args, QStringLiteral("session.redo"), requestedCount, countError))
    {
        return countError;
    }

    QUndoStack *stack = qApp->getUndoStack();
    int redone = 0;
    for (int i = 0; i < requestedCount && stack->canRedo(); ++i)
    {
        stack->redo(); // Runs the one QUndoCommand (or macro) at the current index's own redo(); see AddToCalc::redo() etc. for what that actually rewrites.
        ++redone;
    }

    return ActionResult::success(buildStepResultPayload(QStringLiteral("redone"), redone, stack));
}

// Implements "session.undoStatus": see session_undo_handlers.h for the documented JSON shape.
ActionResult handleSessionUndoStatus(const QJsonObject &args, const ActionContext &ctx)
{
    Q_UNUSED(args) // session.undoStatus takes no arguments.
    Q_UNUSED(ctx)  // qApp->getUndoStack() is qApp-scoped, not ActionContext-scoped -- see this file's own header comment.

    QUndoStack *stack = qApp->getUndoStack();
    const int index = stack->index();
    const int count = stack->count();

    // Bounded window, not the whole stack: a long-running daemon session (Phase 9) could
    // accumulate an arbitrarily large number of macros over its lifetime, and a caller asking
    // "what am I about to undo/redo" only ever needs the immediate neighborhood of the current
    // position -- see this op's own header comment for the exact "applied" semantics below.
    static const int windowRadius = 5;
    const int start = std::max(0, index - windowRadius);
    const int end = std::min(count, index + windowRadius); // Exclusive upper bound, matching QUndoStack::text(i)'s valid range [0, count).

    QJsonArray labels;
    for (int i = start; i < end; ++i)
    {
        QJsonObject entry;
        entry[QStringLiteral("index")] = i;
        entry[QStringLiteral("label")] = stack->text(i);
        entry[QStringLiteral("applied")] = (i < index); // See this op's own header comment for exactly what "applied" means at the boundary.
        labels.append(entry);
    }

    QJsonObject payload;
    payload[QStringLiteral("canUndo")] = stack->canUndo();
    payload[QStringLiteral("canRedo")] = stack->canRedo();
    payload[QStringLiteral("index")] = index;
    payload[QStringLiteral("count")] = count;
    payload[QStringLiteral("labels")] = labels;
    return ActionResult::success(payload);
}
