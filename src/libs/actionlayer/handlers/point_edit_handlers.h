//---------------------------------------------------------------------------------------------------------------------
//  @file   point_edit_handlers.h
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

#ifndef POINT_EDIT_HANDLERS_H // Include guard start, prevents this header being processed twice in one translation unit.
#define POINT_EDIT_HANDLERS_H // Marks POINT_EDIT_HANDLERS_H as defined for the remainder of the include guard.

#include "../action_result.h" // Provides ActionResult, this handler's return type.

class QJsonObject;   // Forward declaration; only used by const reference in the signature below.
class ActionContext; // Forward declaration; only used by const reference in the signature below.

// Implements "point.edit": {"name", "x"?, "y"?, "length"?, "angle"?} -> {"id","name","updated"}.
// The first edit-in-place action in the action layer -- every op before this one only creates new
// objects. Mutates an *existing* point's stored formula/coordinates and drives the same
// DOM-write-plus-recompute path a GUI dialog's "Apply" click uses (VDrawTool::SaveOption(), via
// each tool's own already-public setter -- see the setters below), rather than a fresh Create()
// call, so the point's id (and every other object's, e.g. line endpoints referencing it) never
// changes.
//
// Which fields apply depends on how the named point was created:
//   - "x"/"y": only a basePoint-created point (VToolBasePoint) -- both required together, in the
//     pattern's current working unit (matching VToolBasePoint::SetBasePointPos()'s own contract:
//     it always converts via qApp->toPixel(), the current pattern unit, with no per-call
//     override).
//   - "length": any point created by a formula-length tool (VToolLinePoint subclass: endLine,
//     alongLine, normal, bisector, shoulderPoint) -- a formula string, passed straight through
//     to VFormula/qmuparser exactly like every point-creating op already does, never
//     pre-resolved to a number here.
//   - "angle": currently only an endLine-created point (VToolEndLine) -- also a formula string.
//     normal/bisector/shoulderPoint use a plain numeric angle *offset* (not a formula) with no
//     public setter exposed the same way; curveIntersectAxis/lineIntersectAxis have their own
//     formula-angle setters. Extending "angle" editing to those tool types is a documented
//     follow-up, not silently attempted here.
// A field present in args but unsupported for the named point's actual tool type produces a
// structured "unsupported" failure naming which field and why, rather than a silent no-op.
ActionResult handlePointEdit(const QJsonObject &args, const ActionContext &ctx); // Implemented in point_edit_handlers.cpp.

#endif // POINT_EDIT_HANDLERS_H // End of include guard started above.
