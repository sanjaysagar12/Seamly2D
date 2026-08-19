//---------------------------------------------------------------------------------------------------------------------
//  @file   pattern_resolve_name_handler.cpp
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

#include "pattern_resolve_name_handler.h" // Brings in the ActionResult-returning handlePatternResolveName declaration this file implements.

#include "../action_context.h" // Brings in ActionContext, supplying the data container for this handler.
#include "../name_resolver.h"  // Brings in NameResolver::idForName(); ActionResolverError propagates uncaught to ActionEngine::run().
#include "pattern_dump_handler.h" // Brings in goTypeToString(), reused here so both ops report identical type labels.

#include "../../vpatterndb/vcontainer.h" // Brings in VContainer::GeometricObject<T>().
#include "../../vgeometry/vgobject.h"    // Brings in VGObject::getType().

#include <QJsonObject> // Provides QJsonObject, used for both the input args and the output payload.

// Implements "pattern.resolveName": resolves the "name" argument to an internal id via
// NameResolver, then reports {"name", "id", "type"}. An unknown name is not handled here -- it
// surfaces as ActionResolverError, which propagates up to ActionEngine::run() and is serialized
// into the structured {"type": "nameResolution", ...} error shape there.
ActionResult handlePatternResolveName(const QJsonObject &args, const ActionContext &ctx)
{
    const QString name = args.value("name").toString(); // The user-visible name to resolve; empty if absent or not a string.
    if (name.isEmpty()) // A missing/blank name is a caller mistake, not a resolution failure -- report it directly, not via ActionResolverError.
    {
        return ActionResult::failure(QStringLiteral("pattern.resolveName requires a non-empty \"name\" argument"));
    }

    const VContainer *data = ctx.data(); // Local alias for the pattern's variable/data container.
    if (data == nullptr) // Guard against a context that was constructed without a data container.
    {
        return ActionResult::failure(QStringLiteral("pattern.resolveName: no data container available in this context"));
    }

    const quint32 id = NameResolver::idForName(name, data); // Throws ActionResolverError on an unknown name; not caught here by design.
    const QSharedPointer<VGObject> obj = data->GeometricObject<VGObject>(id); // id was just confirmed present, so this will not throw in practice.

    QJsonObject payload; // Builds the documented {"name", "id", "type"} result.
    payload["name"] = name; // Echoes the resolved name back, for a caller that only kept the id around.
    payload["id"] = static_cast<qint64>(id); // Widen quint32 losslessly into qint64 for JSON.
    payload["type"] = goTypeToString(obj->getType()); // Machine-readable geometry-type label, shared with pattern.dump.

    return ActionResult::success(payload); // Wrap the payload as a successful result for the registry/engine to return.
}
