//---------------------------------------------------------------------------------------------------------------------
//  @file   cutpoint_handlers.h
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

#ifndef CUTPOINT_HANDLERS_H
#define CUTPOINT_HANDLERS_H

#include "../action_result.h"

class QJsonObject;
class ActionContext;

// Phase 8: the ten "derive one point from existing curves/arcs/points" tools. Every formula
// string is passed straight into the matching VTool*::Create() overload, exactly as
// formula_point_handlers.cpp's six ops already do; three of these ten (pointOfIntersectionArcs,
// pointFromArcAndTangent, triangle) involve no formula at all, the same way lineIntersect (Phase 6)
// does not.

// Implements "cutSpline": {"name","curve","direction","length","lineColor"?,"mx"?,"my"?,
// "showPointName"?} -> {"id","name","op"}. "curve" names any VAbstractCubicBezier-derived curve
// (spline, splinePath, cubicBezier, or cubicBezierPath -- VToolCutSpline::Create() itself accepts
// any of the four via that common base, not literally GOType::Spline only). "direction" is
// "forward" or "backward" (anything other than "forward" is treated as backward, mirroring
// VToolCutSpline::Create()'s own `direction == "forward"` check). Mirrors VToolCutSpline::Create().
ActionResult handleCutSpline(const QJsonObject &args, const ActionContext &ctx);

// Implements "cutArc": {"name","arc","direction","length","lineColor"?,"mx"?,"my"?,
// "showPointName"?} -> {"id","name","op"}. Mirrors VToolCutArc::Create(); "direction" behaves as
// documented for "cutSpline" above.
ActionResult handleCutArc(const QJsonObject &args, const ActionContext &ctx);

// Implements "pointOfIntersectionArcs": {"name","firstArc","secondArc","crossPoint"
// ("firstPoint"|"secondPoint"),"mx"?,"my"?} -> {"id","name","op"}. No formula, no
// "showPointName" (VToolPointOfIntersectionArcs::Create() has no such parameter). Mirrors
// VToolPointOfIntersectionArcs::Create().
ActionResult handlePointOfIntersectionArcs(const QJsonObject &args, const ActionContext &ctx);

// Implements "pointOfIntersectionCircles": {"name","firstCircleCenter","secondCircleCenter",
// "firstCircleRadius","secondCircleRadius","crossPoint" ("firstPoint"|"secondPoint"),"mx"?,"my"?,
// "showPointName"?} -> {"id","name","op"}. Mirrors IntersectCirclesTool::Create().
ActionResult handlePointOfIntersectionCircles(const QJsonObject &args, const ActionContext &ctx);

// Implements "pointOfIntersectionCurves": {"name","firstCurve","secondCurve",
// "vCrossPoint" ("highestPoint"|"lowestPoint"),"hCrossPoint" ("leftmostPoint"|"rightmostPoint"),
// "mx"?,"my"?,"showPointName"?} -> {"id","name","op"}. "firstCurve"/"secondCurve" name any curve
// type. Mirrors VToolPointOfIntersectionCurves::Create().
ActionResult handlePointOfIntersectionCurves(const QJsonObject &args, const ActionContext &ctx);

// Implements "curveIntersectAxis": {"name","basePoint","curve","angle","lineType"?,"lineWeight"?,
// "lineColor"?,"mx"?,"my"?,"showPointName"?} -> {"id","name","op"}. "curve" names any curve type
// (including an arc -- VAbstractCurve encompasses arcs, so this op also covers what the task
// description separately calls "arcIntersectAxis"; see this file's .cpp for why no separate
// handler exists: Tool::ArcIntersectAxis is a reserved enum value with no live tool class in this
// codebase -- confirmed against pattern_dump_handler.cpp's own toolToString() comment). Mirrors
// VToolCurveIntersectAxis::Create().
ActionResult handleCurveIntersectAxis(const QJsonObject &args, const ActionContext &ctx);

// Implements "pointFromCircleAndTangent": {"name","circleCenter","circleRadius","tangentPoint",
// "crossPoint" ("firstPoint"|"secondPoint"),"mx"?,"my"?,"showPointName"?} -> {"id","name","op"}.
// Mirrors IntersectCircleTangentTool::Create().
ActionResult handlePointFromCircleAndTangent(const QJsonObject &args, const ActionContext &ctx);

// Implements "pointFromArcAndTangent": {"name","arc","tangentPoint","crossPoint"
// ("firstPoint"|"secondPoint"),"mx"?,"my"?,"showPointName"?} -> {"id","name","op"}. No formula.
// Mirrors VToolPointFromArcAndTangent::Create().
ActionResult handlePointFromArcAndTangent(const QJsonObject &args, const ActionContext &ctx);

// Implements "triangle": {"name","axisP1","axisP2","firstPoint","secondPoint","mx"?,"my"?,
// "showPointName"?} -> {"id","name","op"}. No formula: finds the point on line axisP1-axisP2
// where a right angle forms with the hypotenuse firstPoint-secondPoint. Mirrors
// VToolTriangle::Create().
ActionResult handleTriangle(const QJsonObject &args, const ActionContext &ctx);

// Implements "height": {"name","lineType"?,"lineWeight"?,"lineColor"?,"basePoint","p1Line",
// "p2Line","mx"?,"my"?,"showPointName"?} -> {"id","name","op"}. No formula: finds the
// perpendicular projection of "basePoint" onto the line p1Line-p2Line. Mirrors
// VToolHeight::Create().
ActionResult handleHeight(const QJsonObject &args, const ActionContext &ctx);

#endif // CUTPOINT_HANDLERS_H
