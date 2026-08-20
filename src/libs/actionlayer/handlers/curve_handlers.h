//---------------------------------------------------------------------------------------------------------------------
//  @file   curve_handlers.h
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

#ifndef CURVE_HANDLERS_H
#define CURVE_HANDLERS_H

#include "../action_result.h"

class QJsonObject;
class ActionContext;

// Phase 8: the seven curve-construction tools. Every formula string is passed straight into the
// matching VTool*::Create() overload exactly as typed by the caller, mirroring
// formula_point_handlers.cpp's contract -- except cubicBezier/cubicBezierPath, which have no
// formula-based Create() overload at all (their curve is defined directly by named control
// points), so those two build the raw VCubicBezier(Path) geometry object and use its id+object
// Create() overload instead, the same idiom point_handlers.cpp's basePoint uses for VPointF.

// Implements "spline": {"point1","point4","angle1","angle2","length1","length2","duplicate"?,
// "lineColor"?,"lineType"?,"lineWeight"?,"autoSmooth"?,"lengthMode"?,"targetLength"?} ->
// {"id","point1","point4","op"}. Mirrors VToolSpline::Create()'s raw-formula overload; "lineType"
// maps onto VToolSpline::Create()'s "penStyle" parameter, kept as "lineType" here for JSON
// consistency with every other curve/line op in this action layer.
ActionResult handleSpline(const QJsonObject &args, const ActionContext &ctx);

// Implements "splinePath": {"points":[{"point","angle1","angle2","length1","length2"}, ...],
// "duplicate"?,"lineColor"?,"lineType"?,"lineWeight"?} -> {"id","points","op"}. Each entry in
// "points" supplies the per-point tangent-angle and handle-length formula quad VToolSplinePath::
// Create()'s raw overload expects (QVector<QString> a1/a2/l1/l2, one entry per point -- not per
// segment). At least two points are required to form one spline segment.
ActionResult handleSplinePath(const QJsonObject &args, const ActionContext &ctx);

// Implements "cubicBezier": {"point1","point2","point3","point4","lineColor"?,"lineType"?,
// "lineWeight"?} -> {"id","point1","point4","op"}. point1/point4 are the curve's endpoints;
// point2/point3 are its control points -- all four must already exist as named points. Mirrors
// VToolCubicBezier::Create()'s id+object overload (there is no formula-based overload for this
// tool: a cubic bezier's shape is defined directly by its four control points).
ActionResult handleCubicBezier(const QJsonObject &args, const ActionContext &ctx);

// Implements "cubicBezierPath": {"points":["A","CP1","CP2","B","CP3","CP4","C", ...],"lineColor"?,
// "lineType"?,"lineWeight"?} -> {"id","points","op"}. "points" is a flat list of already-named
// points: 4 points for one segment, then 3 more per additional segment (each extra segment reuses
// the previous segment's last point as its own start). Mirrors VToolCubicBezierPath::Create()'s
// id+object overload, for the same reason as cubicBezier above.
ActionResult handleCubicBezierPath(const QJsonObject &args, const ActionContext &ctx);

// Implements "arc": {"center","radius","f1","f2","lineColor"?,"lineType"?,"lineWeight"?} ->
// {"id","op"}. Mirrors VToolArc::Create()'s raw-formula overload.
ActionResult handleArc(const QJsonObject &args, const ActionContext &ctx);

// Implements "arcWithLength": {"center","radius","f1","length","lineColor"?,"lineType"?,
// "lineWeight"?} -> {"id","op"}. Same as "arc" but the arc's angular extent is derived from a
// "length" (arc length) formula instead of a second angle "f2". Mirrors
// VToolArcWithLength::Create()'s raw-formula overload.
ActionResult handleArcWithLength(const QJsonObject &args, const ActionContext &ctx);

// Implements "ellipticalArc": {"center","radius1","radius2","f1","f2","rotationAngle","lineColor"?,
// "lineType"?,"lineWeight"?} -> {"id","op"}. Mirrors VToolEllipticalArc::Create()'s raw-formula
// overload.
ActionResult handleEllipticalArc(const QJsonObject &args, const ActionContext &ctx);

#endif // CURVE_HANDLERS_H
