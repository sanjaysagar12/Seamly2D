//---------------------------------------------------------------------------------------------------------------------
//  @file   pattern_resolve_name_handler.h
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

#ifndef PATTERN_RESOLVE_NAME_HANDLER_H // Include guard start, prevents this header being processed twice in one translation unit.
#define PATTERN_RESOLVE_NAME_HANDLER_H // Marks PATTERN_RESOLVE_NAME_HANDLER_H as defined for the remainder of the include guard.

#include "../action_result.h" // Provides ActionResult, this handler's return type.

class QJsonObject;   // Forward declaration; only used by const reference in the signature below.
class ActionContext; // Forward declaration; only used by const reference in the signature below.

// Implements the "pattern.resolveName" op: resolves a pattern object's user-visible name to its
// internal id and type, e.g. {"op": "pattern.resolveName", "name": "A1"} ->
// {"name": "A1", "id": 47, "type": "Point"}. Read-only; exists mainly so Phase 4's name-resolution
// layer (NameResolver) has a JSON-reachable action to exercise in integration tests, ahead of
// Phase 5's mutating handlers that will consume names the same way internally.
ActionResult handlePatternResolveName(const QJsonObject &args, const ActionContext &ctx); // Implemented in pattern_resolve_name_handler.cpp.

#endif // PATTERN_RESOLVE_NAME_HANDLER_H // End of include guard started above.
