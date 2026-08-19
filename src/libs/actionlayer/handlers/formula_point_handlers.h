//---------------------------------------------------------------------------------------------------------------------
//  @file   formula_point_handlers.h
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

#ifndef FORMULA_POINT_HANDLERS_H // Include guard start, prevents this header being processed twice in one translation unit.
#define FORMULA_POINT_HANDLERS_H // Marks FORMULA_POINT_HANDLERS_H as defined for the remainder of the include guard.

#include "../action_result.h" // Provides ActionResult, the return type every handler below produces.

class QJsonObject;   // Forward declaration; only used by const reference in the signatures below.
class ActionContext; // Forward declaration; only used by const reference in the signatures below.

// Phase 6: the six formula-bearing (or, for lineIntersect, purely geometric) point tools. Every
// formula string here is passed straight into the matching VTool*::Create() overload exactly as
// typed by the caller -- never pre-evaluated to a number in the handler -- so the resulting point
// stays measurement-reactive (a later "pattern.dump" or re-run against different measurements
// re-evaluates the same stored formula), the same contract the XML file parser itself relies on.

// Implements "endLine": {"name","basePoint","length","angle","lineType"?,"lineWeight"?,"lineColor"?,
// "mx"?,"my"?,"showPointName"?} -> {"id","name","op"}. Mirrors VToolEndLine::Create()'s own
// two-formula (length + angle) recipe for "a point at distance+angle from a base point".
ActionResult handleEndLine(const QJsonObject &args, const ActionContext &ctx); // Implemented in formula_point_handlers.cpp.

// Implements "alongLine": {"name","firstPoint","secondPoint","length","lineType"?,"lineWeight"?,
// "lineColor"?,"mx"?,"my"?,"showPointName"?} -> {"id","name","op"}. Mirrors VToolAlongLine::Create().
ActionResult handleAlongLine(const QJsonObject &args, const ActionContext &ctx); // Implemented in formula_point_handlers.cpp.

// Implements "normal": {"name","firstPoint","secondPoint","length","angle"?,"lineType"?,
// "lineWeight"?,"lineColor"?,"mx"?,"my"?,"showPointName"?} -> {"id","name","op"}. Mirrors
// VToolNormal::Create() ("angle" here is a plain numeric rotation offset, not a formula -- see
// VToolNormal::Create()'s own "const qreal angle" parameter, distinct from "length"'s formula).
ActionResult handleNormal(const QJsonObject &args, const ActionContext &ctx); // Implemented in formula_point_handlers.cpp.

// Implements "bisector": {"name","firstPoint","secondPoint","thirdPoint","length","lineType"?,
// "lineWeight"?,"lineColor"?,"mx"?,"my"?,"showPointName"?} -> {"id","name","op"}. Mirrors
// VToolBisector::Create().
ActionResult handleBisector(const QJsonObject &args, const ActionContext &ctx); // Implemented in formula_point_handlers.cpp.

// Implements "shoulderPoint": {"name","p1Line","p2Line","pShoulder","length","lineType"?,
// "lineWeight"?,"lineColor"?,"mx"?,"my"?,"showPointName"?} -> {"id","name","op"}. Mirrors
// VToolShoulderPoint::Create().
ActionResult handleShoulderPoint(const QJsonObject &args, const ActionContext &ctx); // Implemented in formula_point_handlers.cpp.

// Implements "lineIntersect": {"name","p1Line1","p2Line1","p1Line2","p2Line2","mx"?,"my"?,
// "showPointName"?} -> {"id","name","op"}. No formula involved (pure line/line intersection);
// included alongside the formula-bearing five because it completes the "derive a point from two
// existing lines" family. Mirrors VToolLineIntersect::Create().
ActionResult handleLineIntersect(const QJsonObject &args, const ActionContext &ctx); // Implemented in formula_point_handlers.cpp.

#endif // FORMULA_POINT_HANDLERS_H // End of include guard started above.
