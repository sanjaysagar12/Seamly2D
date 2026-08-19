//---------------------------------------------------------------------------------------------------------------------
//  @file   pattern_list_tools_handler.cpp
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

#include "pattern_list_tools_handler.h" // Brings in the ActionResult-returning handleListTools declaration this file implements.

#include "../action_context.h" // Brings in ActionContext; unused by this handler's body but required by the shared signature.

#include <QJsonArray>  // Provides QJsonArray, used to build the "tools" JSON array.
#include <QJsonObject> // Provides QJsonObject, the payload's top-level shape.

// Implements "pattern.listTools": returns the static list of op names this action layer
// currently supports, as {"tools": [...]}. This list is hand-written and MUST be updated
// in the same change that registers or removes an op in ActionRegistry -- it is Phase 1's
// source of truth for "what can I call right now", not a reflection of the registry's
// internal QHash.
ActionResult handleListTools(const QJsonObject &args, const ActionContext &ctx)
{
    Q_UNUSED(args) // pattern.listTools takes no arguments in Phase 1; kept for signature uniformity with other handlers.
    Q_UNUSED(ctx)  // This handler reports static capability data and does not touch the pattern; ctx is unused.

    QJsonArray tools; // Accumulates the hand-written list of currently supported op names.
    tools.append(QStringLiteral("pattern.dump"));             // Read-only geometry + history dump, implemented in pattern_dump_handler.cpp.
    tools.append(QStringLiteral("pattern.listMeasurements")); // Read-only measurement listing, implemented in pattern_measurements_handler.cpp.
    tools.append(QStringLiteral("pattern.listTools"));        // This op itself, so callers can discover it via introspection too.
    tools.append(QStringLiteral("render.snapshot"));          // Scene-to-image rendering, implemented in render_handlers.cpp.
    tools.append(QStringLiteral("pattern.resolveName"));      // Phase 4: name -> id/type resolution, implemented in pattern_resolve_name_handler.cpp.

    QJsonObject payload; // Wraps the array under its documented output key.
    payload["tools"] = tools; // "tools": every action op this action layer currently supports.

    return ActionResult::success(payload); // Wrap the payload as a successful result for the registry/engine to return.
}
