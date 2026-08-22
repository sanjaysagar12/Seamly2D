//---------------------------------------------------------------------------------------------------------------------
//  @file   piece_dump_handler.h
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

#ifndef PIECE_DUMP_HANDLER_H
#define PIECE_DUMP_HANDLER_H

#include "../action_result.h"

class QJsonObject;
class ActionContext;

// Implements "piece.dump": {"piece": "<name or id>"} -> a read-only snapshot of one piece's main
// outline path, internal paths, and anchor points, as
// {"id","name","op","seamAllowance","seamAllowanceWidthFormula","nodes","internalPaths","anchors"}.
// Never mutates the pattern. "piece" accepts either a JSON string (matched against VPiece::GetName())
// or a JSON number (taken as a literal piece id) -- see piece_dump_handler.cpp's own
// resolvePieceRefArg() for the exact matching rules, the same name-or-id acceptance
// render.snapshot's "target": "piece" uses for its own "piece" argument.
//
// Each node in "nodes"/"internalPaths[].nodes" reports {"id","type","reverse","name"?,"x"?,"y"?,
// "unsupported"?}: "type" is the node's own Tool enumerator name (e.g. "NodePoint", "NodeArc");
// "name"/"x"/"y" are only present when the underlying object could be resolved (and, for "x"/"y",
// is itself a point) -- mirrors pattern.dump's own choice of what to report per point. "unsupported"
// is set to true for any node whose type this action layer's own piece.addPatternPiece/
// piece.internalPath cannot construct today (anything other than "NodePoint" -- see
// piece_handlers.h's own documented gap comment), or whose underlying object could not be resolved
// at all -- reading such a node never crashes or silently drops it from the response, it is always
// reported (id/type/reverse at minimum), just flagged.
ActionResult handlePieceDump(const QJsonObject &args, const ActionContext &ctx); // Implemented in piece_dump_handler.cpp.

#endif // PIECE_DUMP_HANDLER_H
