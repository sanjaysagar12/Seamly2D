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
    tools.append(QStringLiteral("basePoint"));                 // Phase 5: mutating base-point/draft-block creation, implemented in point_handlers.cpp.
    tools.append(QStringLiteral("line"));                       // Phase 5: mutating line creation, implemented in line_handlers.cpp.
    tools.append(QStringLiteral("endLine"));                    // Phase 6: mutating formula point-at-distance+angle creation, implemented in formula_point_handlers.cpp.
    tools.append(QStringLiteral("alongLine"));                  // Phase 6: mutating formula point-along-line creation, implemented in formula_point_handlers.cpp.
    tools.append(QStringLiteral("normal"));                     // Phase 6: mutating formula point-via-normal creation, implemented in formula_point_handlers.cpp.
    tools.append(QStringLiteral("bisector"));                   // Phase 6: mutating formula bisector-point creation, implemented in formula_point_handlers.cpp.
    tools.append(QStringLiteral("shoulderPoint"));               // Phase 6: mutating formula shoulder-point creation, implemented in formula_point_handlers.cpp.
    tools.append(QStringLiteral("lineIntersect"));               // Phase 6: mutating line/line intersection point creation, implemented in formula_point_handlers.cpp.
    tools.append(QStringLiteral("measurements.load"));           // Phase 7: mutating measurement-file swap (no recompute), implemented in measurements_sync_handlers.cpp.
    tools.append(QStringLiteral("measurements.recompute"));      // Phase 7: mutating formula/geometry recompute, implemented in measurements_sync_handlers.cpp.
    tools.append(QStringLiteral("measurements.sync"));           // Phase 7: mutating measurements.load + measurements.recompute in one action, implemented in measurements_sync_handlers.cpp.

    // 20 Aug, 2026: backfilled -- this list had fallen out of sync with ActionRegistry since Phase
    // 8 registered all of the below without updating it (a pre-existing gap, not introduced by
    // this change); fixed here alongside adding this change's own three new ops below.
    tools.append(QStringLiteral("spline"));                      // Phase 8: mutating quadratic spline creation, implemented in curve_handlers.cpp.
    tools.append(QStringLiteral("splinePath"));                  // Phase 8: mutating multi-point spline path creation, implemented in curve_handlers.cpp.
    tools.append(QStringLiteral("cubicBezier"));                 // Phase 8: mutating cubic bezier curve creation, implemented in curve_handlers.cpp.
    tools.append(QStringLiteral("cubicBezierPath"));             // Phase 8: mutating multi-point cubic bezier path creation, implemented in curve_handlers.cpp.
    tools.append(QStringLiteral("arc"));                         // Phase 8: mutating arc-by-radius creation, implemented in curve_handlers.cpp.
    tools.append(QStringLiteral("arcWithLength"));               // Phase 8: mutating arc-by-length creation, implemented in curve_handlers.cpp.
    tools.append(QStringLiteral("ellipticalArc"));               // Phase 8: mutating elliptical arc creation, implemented in curve_handlers.cpp.
    tools.append(QStringLiteral("cutSpline"));                   // Phase 8: mutating cut-spline point creation, implemented in cutpoint_handlers.cpp.
    tools.append(QStringLiteral("cutArc"));                      // Phase 8: mutating cut-arc point creation, implemented in cutpoint_handlers.cpp.
    tools.append(QStringLiteral("pointOfIntersectionArcs"));     // Phase 8: mutating arc/arc intersection point creation, implemented in cutpoint_handlers.cpp.
    tools.append(QStringLiteral("pointOfIntersectionCircles"));  // Phase 8: mutating circle/circle intersection point creation, implemented in cutpoint_handlers.cpp.
    tools.append(QStringLiteral("pointOfIntersectionCurves"));   // Phase 8: mutating curve/curve intersection point creation, implemented in cutpoint_handlers.cpp.
    tools.append(QStringLiteral("curveIntersectAxis"));          // Phase 8: mutating curve/axis intersection point creation, implemented in cutpoint_handlers.cpp.
    tools.append(QStringLiteral("pointFromCircleAndTangent"));   // Phase 8: mutating circle-and-tangent point creation, implemented in cutpoint_handlers.cpp.
    tools.append(QStringLiteral("pointFromArcAndTangent"));      // Phase 8: mutating arc-and-tangent point creation, implemented in cutpoint_handlers.cpp.
    tools.append(QStringLiteral("triangle"));                    // Phase 8: mutating triangle-point creation, implemented in cutpoint_handlers.cpp.
    tools.append(QStringLiteral("height"));                      // Phase 8: mutating height (perpendicular) point creation, implemented in cutpoint_handlers.cpp.
    tools.append(QStringLiteral("move"));                        // Phase 8: mutating move operation, implemented in operation_handlers.cpp.
    tools.append(QStringLiteral("rotation"));                    // Phase 8: mutating rotation operation, implemented in operation_handlers.cpp.
    tools.append(QStringLiteral("mirrorByLine"));                // Phase 8: mutating mirror-by-line operation, implemented in operation_handlers.cpp.
    tools.append(QStringLiteral("mirrorByAxis"));                // Phase 8: mutating mirror-by-axis operation, implemented in operation_handlers.cpp.
    tools.append(QStringLiteral("group"));                       // Phase 8: mutating object-group creation, implemented in operation_handlers.cpp.
    tools.append(QStringLiteral("trueDarts"));                   // Phase 8: mutating true-darts creation, implemented in operation_handlers.cpp.
    tools.append(QStringLiteral("piece.addPatternPiece"));       // Phase 8: mutating pattern-piece assembly, implemented in piece_handlers.cpp.
    tools.append(QStringLiteral("piece.addAnchorPoint"));        // Phase 8: mutating piece anchor-point creation, implemented in piece_handlers.cpp.
    tools.append(QStringLiteral("piece.internalPath"));          // Phase 8: mutating piece internal-path creation, implemented in piece_handlers.cpp.
    tools.append(QStringLiteral("piece.insertNodes"));           // Phase 8: mutating piece node insertion, implemented in piece_handlers.cpp.
    tools.append(QStringLiteral("piece.union"));                 // Phase 8: mutating piece union -- KNOWN GAP, see piece_handlers.h; do not rely on it yet.
    tools.append(QStringLiteral("point.edit"));                  // 20 Aug, 2026: mutating edit-in-place for an existing point's formula/coordinates, implemented in point_edit_handlers.cpp.
    tools.append(QStringLiteral("session.save"));                // 20 Aug, 2026: in-script pattern save, implemented in session_handlers.cpp.
    tools.append(QStringLiteral("session.close"));                // 20 Aug, 2026: session-lifecycle no-op, implemented in session_handlers.cpp.
    tools.append(QStringLiteral("session.undo"));                 // Phase 12 (22 Aug, 2026): undo the last N mutating actions, implemented in session_undo_handlers.cpp.
    tools.append(QStringLiteral("session.redo"));                 // Phase 12 (22 Aug, 2026): redo the last N undone actions, implemented in session_undo_handlers.cpp.
    tools.append(QStringLiteral("session.undoStatus"));           // Phase 12 (22 Aug, 2026): read-only undo-stack introspection, implemented in session_undo_handlers.cpp.

    QJsonObject payload; // Wraps the array under its documented output key.
    payload["tools"] = tools; // "tools": every action op this action layer currently supports.

    return ActionResult::success(payload); // Wrap the payload as a successful result for the registry/engine to return.
}
