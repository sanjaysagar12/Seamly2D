//---------------------------------------------------------------------------------------------------------------------
//  @file   action_registry.cpp
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

#include "action_registry.h" // Brings in the ActionRegistry class declaration this file implements.

#include "handlers/pattern_dump_handler.h"         // Brings in handlePatternDump(), registered under "pattern.dump".
#include "handlers/pattern_measurements_handler.h" // Brings in handleListMeasurements(), registered under "pattern.listMeasurements".
#include "handlers/pattern_list_tools_handler.h"   // Brings in handleListTools(), registered under "pattern.listTools".
#include "handlers/render_handlers.h"              // Brings in handleRenderSnapshot(), registered under "render.snapshot".
#include "handlers/pattern_resolve_name_handler.h" // Brings in handlePatternResolveName(), registered under "pattern.resolveName".
#include "handlers/point_handlers.h"                // Brings in handleBasePoint(), registered under "basePoint".
#include "handlers/line_handlers.h"                 // Brings in handleLine(), registered under "line".
#include "handlers/formula_point_handlers.h"        // Brings in the six Phase 6 handle*() functions below (endLine, alongLine, normal, bisector, shoulderPoint, lineIntersect).
#include "handlers/measurements_sync_handlers.h"    // Brings in the three Phase 7 handle*() functions below (measurements.load, .recompute, .sync).
#include "handlers/curve_handlers.h"                 // Brings in the seven Phase 8 curve handle*() functions below.
#include "handlers/cutpoint_handlers.h"               // Brings in the ten Phase 8 cut/intersection point handle*() functions below.
#include "handlers/operation_handlers.h"              // Brings in the six Phase 8 operation handle*() functions below (move, rotation, mirrorByLine, mirrorByAxis, group, trueDarts).
#include "handlers/piece_handlers.h"                  // Brings in the five Phase 8 piece.* handle*() functions below.
#include "handlers/point_edit_handlers.h"              // Brings in handlePointEdit(), registered under "point.edit".
#include "handlers/session_handlers.h"                 // Brings in handleSessionSave()/handleSessionClose(), registered under "session.save"/"session.close".

// Constructs an empty handler map, then immediately populates it with every built-in handler.
ActionRegistry::ActionRegistry()
{
    registerBuiltinActions(); // Every freshly constructed registry is ready to use without extra setup calls.
}

// Registers every built-in handler under its documented op name. This is the one place that
// lists every currently-registered op; pattern_list_tools_handler.cpp keeps its own static,
// hand-written mirror of these names for the "pattern.listTools" response.
void ActionRegistry::registerBuiltinActions()
{
    registerAction(QStringLiteral("pattern.dump"), &handlePatternDump);                     // Geometry + history introspection.
    registerAction(QStringLiteral("pattern.listMeasurements"), &handleListMeasurements);    // Measurement variable listing.
    registerAction(QStringLiteral("pattern.listTools"), &handleListTools);                  // Static op-name capability listing.
    registerAction(QStringLiteral("render.snapshot"), &handleRenderSnapshot);               // Scene-to-image rendering.
    registerAction(QStringLiteral("pattern.resolveName"), &handlePatternResolveName);       // Phase 4: name -> id/type resolution diagnostic.
    registerAction(QStringLiteral("basePoint"), &handleBasePoint);                          // Phase 5: mutating -- creates a new draft block's anchor point.
    registerAction(QStringLiteral("line"), &handleLine);                                    // Phase 5: mutating -- connects two named points with a line.
    registerAction(QStringLiteral("endLine"), &handleEndLine);                              // Phase 6: mutating -- point at formula distance+angle from a base point.
    registerAction(QStringLiteral("alongLine"), &handleAlongLine);                          // Phase 6: mutating -- point at formula distance along an existing line.
    registerAction(QStringLiteral("normal"), &handleNormal);                                // Phase 6: mutating -- point at formula distance along the normal to a line.
    registerAction(QStringLiteral("bisector"), &handleBisector);                            // Phase 6: mutating -- point at formula distance along an angle bisector.
    registerAction(QStringLiteral("shoulderPoint"), &handleShoulderPoint);                  // Phase 6: mutating -- point at formula distance from a shoulder point toward a line.
    registerAction(QStringLiteral("lineIntersect"), &handleLineIntersect);                  // Phase 6: mutating -- point at the intersection of two existing lines.
    registerAction(QStringLiteral("measurements.load"), &handleMeasurementsLoad);           // Phase 7: mutating -- swaps the active measurement file, does not recompute geometry.
    registerAction(QStringLiteral("measurements.recompute"), &handleMeasurementsRecompute); // Phase 7: mutating -- re-evaluates every formula and rebuilds geometry.
    registerAction(QStringLiteral("measurements.sync"), &handleMeasurementsSync);           // Phase 7: mutating -- measurements.load immediately followed by measurements.recompute.

    // Phase 8: curve-construction tools.
    registerAction(QStringLiteral("spline"), &handleSpline);
    registerAction(QStringLiteral("splinePath"), &handleSplinePath);
    registerAction(QStringLiteral("cubicBezier"), &handleCubicBezier);
    registerAction(QStringLiteral("cubicBezierPath"), &handleCubicBezierPath);
    registerAction(QStringLiteral("arc"), &handleArc);
    registerAction(QStringLiteral("arcWithLength"), &handleArcWithLength);
    registerAction(QStringLiteral("ellipticalArc"), &handleEllipticalArc);

    // Phase 8: cut/intersection point tools.
    registerAction(QStringLiteral("cutSpline"), &handleCutSpline);
    registerAction(QStringLiteral("cutArc"), &handleCutArc);
    registerAction(QStringLiteral("pointOfIntersectionArcs"), &handlePointOfIntersectionArcs);
    registerAction(QStringLiteral("pointOfIntersectionCircles"), &handlePointOfIntersectionCircles);
    registerAction(QStringLiteral("pointOfIntersectionCurves"), &handlePointOfIntersectionCurves);
    registerAction(QStringLiteral("curveIntersectAxis"), &handleCurveIntersectAxis);
    registerAction(QStringLiteral("pointFromCircleAndTangent"), &handlePointFromCircleAndTangent);
    registerAction(QStringLiteral("pointFromArcAndTangent"), &handlePointFromArcAndTangent);
    registerAction(QStringLiteral("triangle"), &handleTriangle);
    registerAction(QStringLiteral("height"), &handleHeight);

    // Phase 8: object-transforming operations + group bookkeeping.
    registerAction(QStringLiteral("move"), &handleMove);
    registerAction(QStringLiteral("rotation"), &handleRotation);
    registerAction(QStringLiteral("mirrorByLine"), &handleMirrorByLine);
    registerAction(QStringLiteral("mirrorByAxis"), &handleMirrorByAxis);
    registerAction(QStringLiteral("group"), &handleGroup);
    registerAction(QStringLiteral("trueDarts"), &handleTrueDarts);

    // Phase 8: pattern-piece assembly.
    registerAction(QStringLiteral("piece.addPatternPiece"), &handlePieceAddPatternPiece);
    registerAction(QStringLiteral("piece.addAnchorPoint"), &handlePieceAddAnchorPoint);
    registerAction(QStringLiteral("piece.internalPath"), &handlePieceInternalPath);
    registerAction(QStringLiteral("piece.insertNodes"), &handlePieceInsertNodes);
    registerAction(QStringLiteral("piece.union"), &handlePieceUnion);

    // 20 Aug, 2026: edit-in-place and session-lifecycle actions, backing the persistent NDJSON daemon (session_server.cpp).
    registerAction(QStringLiteral("point.edit"), &handlePointEdit);         // Mutates an existing point's stored formula/coordinates in place (no new id).
    registerAction(QStringLiteral("session.save"), &handleSessionSave);     // In-script equivalent of the one-shot CLI's --save-pattern flag.
    registerAction(QStringLiteral("session.close"), &handleSessionClose);   // No-op action; SessionServer's read loop exits after this batch's response.
}

// Stores the handler function in the internal map under the given name.
void ActionRegistry::registerAction(const QString &name, ActionFn fn)
{
    m_actions.insert(name, fn); // QHash::insert adds the entry, replacing any existing one with the same key.
}

// Reports whether a handler has been registered for the given name.
bool ActionRegistry::hasAction(const QString &name) const
{
    return m_actions.contains(name); // QHash::contains performs the presence check.
}

// Looks up the handler registered under the given name, if any.
ActionRegistry::ActionFn ActionRegistry::action(const QString &name) const
{
    return m_actions.value(name); // QHash::value returns a default-constructed (empty/falsy) ActionFn when the key is absent.
}
