//---------------------------------------------------------------------------------------------------------------------
//  @file   session_handlers.cpp
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

#include "session_handlers.h" // Brings in the ActionResult-returning handleSessionSave/handleSessionClose declarations this file implements.

#include "../action_context.h" // Brings in ActionContext, supplying the document this handler saves.

#include "../../ifc/xml/vabstractpattern.h" // Brings in VAbstractPattern (a VDomDocument subclass): SaveDocument() is declared on the latter, callable straight through the former's pointer.

#include <QDir>        // Provides QDir::mkpath(), used to create the output path's parent directory, matching render_handlers.cpp's own convention.
#include <QFileInfo>   // Provides QFileInfo, used both to create the parent directory and to report the resolved absolute path.
#include <QJsonObject> // Provides QJsonObject, used for both the input args and the output payload.

// Implements "session.save": see session_handlers.h for the documented JSON shape.
ActionResult handleSessionSave(const QJsonObject &args, const ActionContext &ctx)
{
    const QString path = args.value(QStringLiteral("path")).toString();
    if (path.isEmpty())
    {
        return ActionResult::failure(QStringLiteral("session.save requires a non-empty \"path\""));
    }

    VAbstractPattern *doc = ctx.doc();
    if (doc == nullptr)
    {
        return ActionResult::failure(QStringLiteral("session.save: context is missing a document"));
    }

    const QFileInfo pathInfo(path); // Resolves path against the process's current working directory when it's relative -- see this op's own header comment.
    if (!QDir().mkpath(pathInfo.absolutePath())) // Create the parent directory if it doesn't already exist; mkpath() also returns true if it already exists.
    {
        return ActionResult::failure(QStringLiteral("could not create output directory: %1").arg(pathInfo.absolutePath()));
    }

    QString error; // SaveDocument() reports failure via this out-parameter, not an exception.
    if (!doc->SaveDocument(pathInfo.absoluteFilePath(), error))
    {
        return ActionResult::failure(QStringLiteral("failed to save pattern to %1: %2").arg(pathInfo.absoluteFilePath(), error));
    }

    QJsonObject payload;
    payload["path"] = pathInfo.absoluteFilePath();
    return ActionResult::success(payload);
}

// Implements "session.close": see session_handlers.h for the documented JSON shape and for why
// this handler itself does nothing but succeed -- the real effect is in SessionServer's read loop.
ActionResult handleSessionClose(const QJsonObject &args, const ActionContext &ctx)
{
    Q_UNUSED(args) // session.close takes no arguments.
    Q_UNUSED(ctx)  // session.close touches no pattern state.

    QJsonObject payload;
    payload["closing"] = true;
    return ActionResult::success(payload);
}
