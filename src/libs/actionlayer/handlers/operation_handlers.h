//---------------------------------------------------------------------------------------------------------------------
//  @file   operation_handlers.h
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

#ifndef OPERATION_HANDLERS_H
#define OPERATION_HANDLERS_H

#include "../action_result.h"

class QJsonObject;
class ActionContext;

// Phase 8: the object-transforming operations (move/rotate/mirror/trueDarts) plus "group", a pure
// doc-level bookkeeping op with no VTool::Create() factory at all (see operation_handlers.cpp's
// handleGroup() for why). Every op that acts on multiple objects takes a JSON array
// "sourceObjects" of already-named objects (any geometry type, not just points -- these tools
// transform whole lines/arcs/curves, not just points) and returns the new object names/ids in the
// same order via a "created" array. "suffix" is required for move/rotation/mirrorByLine/
// mirrorByAxis: VAbstractOperation-derived tools use it to name each newly-created object from its
// source (e.g. source "A1" + suffix "_m" -> new object "A1_m"), exactly like the interactive
// dialogs' own "Suffix" field.

// Implements "move": {"sourceObjects":[...],"suffix","length","angle","rotationAngle",
// "rotationOrigin"?,"lineType"?,"lineWeight"?,"lineColor"?} -> {"created":[{"id","name"}, ...],
// "op"}. "rotationOrigin" (a point name) is optional: when omitted, VToolMove::Create() derives
// the rotation origin from the source objects' own centroid (see VAbstractOperation::
// findRotationOrigin()). Mirrors VToolMove::Create()'s raw-formula overload.
ActionResult handleMove(const QJsonObject &args, const ActionContext &ctx);

// Implements "rotation": {"sourceObjects":[...],"suffix","origin","angle","lineType"?,
// "lineWeight"?,"lineColor"?} -> {"created":[{"id","name"}, ...],"op"}. "origin" (a point name) is
// required (unlike "move"'s "rotationOrigin"): VToolRotation::Create()'s "origin" parameter has no
// optional/derived-origin path. Mirrors VToolRotation::Create()'s raw-formula overload.
ActionResult handleRotation(const QJsonObject &args, const ActionContext &ctx);

// Implements "mirrorByLine": {"sourceObjects":[...],"suffix","firstLinePoint","secondLinePoint",
// "lineType"?,"lineWeight"?,"lineColor"?} -> {"created":[{"id","name"}, ...],"op"}. Mirrors
// VToolMirrorByLine::Create()'s raw overload (no formula involved: a mirror line is defined by two
// existing points, not a formula).
ActionResult handleMirrorByLine(const QJsonObject &args, const ActionContext &ctx);

// Implements "mirrorByAxis": {"sourceObjects":[...],"suffix","originPoint","axisType"
// ("vertical"|"horizontal"),"lineType"?,"lineWeight"?,"lineColor"?} -> {"created":[{"id","name"},
// ...],"op"}. Mirrors VToolMirrorByAxis::Create()'s raw overload.
ActionResult handleMirrorByAxis(const QJsonObject &args, const ActionContext &ctx);

// Implements "group": {"name","sourceObjects":[...],"color"?,"lineType"?,"lineWeight"?} ->
// {"id","name","op"}. Unlike every other op in this file, "group" has no VTool subclass or
// Create() factory at all -- a group is pure VAbstractPattern (doc) DOM bookkeeping
// (createGroup()/createGroups()/parseGroups(), see groups_widget.cpp's GroupsWidget::
// addGroupToList() and vtools/undocommands/addgroup.cpp's AddGroup::redo(), whose non-undo-stack
// logic this handler reproduces directly). Each "sourceObjects" entry is recorded under the
// group with its own id used as both the map's tool-id and object-id key (the simple case every
// non-composite selected object in the GUI's own group dialog produces); a source object that is
// itself a multi-part tool (e.g. one endpoint of a two-point tool) is not separately addressable
// via this action layer yet -- a documented gap, not a silent mismatch.
ActionResult handleGroup(const QJsonObject &args, const ActionContext &ctx);

// Implements "trueDarts": {"point1Name","point2Name","baseLineP1","baseLineP2","dartP1","dartP2",
// "dartP3","mx1"?,"my1"?,"showPointName1"?,"mx2"?,"my2"?,"showPointName2"?} ->
// {"id","point1Name","point2Name","op"}. Finds the true (folded) positions of a dart's two base
// points once the dart itself is closed. Mirrors VToolTrueDarts::Create().
ActionResult handleTrueDarts(const QJsonObject &args, const ActionContext &ctx);

#endif // OPERATION_HANDLERS_H
