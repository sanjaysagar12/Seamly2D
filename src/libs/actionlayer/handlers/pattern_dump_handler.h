//---------------------------------------------------------------------------------------------------------------------
//  @file   pattern_dump_handler.h
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

#ifndef PATTERN_DUMP_HANDLER_H // Include guard start, prevents this header being processed twice in one translation unit.
#define PATTERN_DUMP_HANDLER_H // Marks PATTERN_DUMP_HANDLER_H as defined for the remainder of the include guard.

#include "../action_result.h" // Provides ActionResult, this handler's return type.

#include "../../vgeometry/vgeometrydef.h" // Provides the GOType enum, goTypeToString()'s parameter type; trivial header (enums only), safe to include directly.
#include "../../vmisc/def.h"              // Provides the Tool enum, toolToString()'s parameter type; trivial header (enums only), safe to include directly.

#include <QString> // Provides QString, goTypeToString()/toolToString()'s return type.

class QJsonObject;   // Forward declaration; only used by const reference in the signature below.
class ActionContext; // Forward declaration; only used by const reference in the signature below.

// Implements the "pattern.dump" op: a read-only introspection dump of every geometry object
// in the pattern's data container plus the tool history, as a single JSON payload.
ActionResult handlePatternDump(const QJsonObject &args, const ActionContext &ctx); // Implemented in pattern_dump_handler.cpp.

// Converts a GOType enumerator to its exact C++ name (e.g. GOType::Point -> "Point"). Exposed here
// (rather than kept file-local) so "pattern.resolveName" can report the same type label pattern.dump
// does, instead of a second, potentially-drifting stringification of the same enum.
QString goTypeToString(GOType type); // Implemented in pattern_dump_handler.cpp.

// Converts a Tool enumerator to its exact C++ name (e.g. Tool::NodeArc -> "NodeArc"). Exposed here
// (rather than kept file-local, as it was before piece_dump_handler.cpp needed it) so "piece.dump"
// can report the same per-node tool-type label pattern.dump's own history entries do, instead of a
// second, potentially-drifting stringification of the same enum -- the exact rationale
// goTypeToString() above already documents for GOType.
QString toolToString(Tool tool); // Implemented in pattern_dump_handler.cpp.

#endif // PATTERN_DUMP_HANDLER_H // End of include guard started above.
