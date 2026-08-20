//---------------------------------------------------------------------------------------------------------------------
//  @file   piece_handlers.h
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

#ifndef PIECE_HANDLERS_H
#define PIECE_HANDLERS_H

#include "../action_result.h"

class QJsonObject;
class ActionContext;

// Phase 8: pattern-piece assembly ops. Unlike every other handler file, these do real assembly
// work (not pure JSON-to-Create() translation): "piece.addPatternPiece" builds a VPiece from an
// ordered node-name list and validates it forms a closed, non-self-intersecting polygon before
// calling PatternPieceTool::Create(). Every op here uses ctx.pieceScene() (Phase 8's new
// ActionContext accessor), not ctx.scene() -- PatternPieceTool/InternalPathTool/AnchorPointTool/
// UnionTool all add their graphics items to the piece-mode scene, mirroring MainWindow's own
// draftScene/pieceScene split.
//
// Only point-type nodes are currently supported by "nodes" arrays in this file (Tool::NodePoint);
// arc/curve piece nodes are a documented gap for a follow-up phase, not a silent mismatch.
//
// FOUND AND PARTIALLY FIXED -- NameResolver/piece-node-clone name collision (found while
// validating tests/actionlayer/cases/03_import_l_shape_to_rectangle as genuine run-to-run
// non-determinism: the same actions.json resolved "basePoint": "B" to a different object id
// across separate process runs of the identical build). Root cause: VAbstractTool::
// CreateNode<VPointF>() copy-constructs each piece node's clone from its source point, which also
// copies the source's name() verbatim (even though the clone's serialized XML
// <point type="modeling"> element never carries a "name" attribute -- only NameResolver's
// in-memory lookup ever reads it). Once any piece exists, NameResolver::idForName()
// (name_resolver.cpp) -- a plain linear scan over VContainer::DataGObjects() with no preference
// between a "calculation" object and a "modeling" clone that happen to share a name -- can
// resolve to either one, depending on QHash's per-process randomized iteration order.
//
// Fixed for FRESH creation: resolvePreparedPointNodes() below immediately renames every clone it
// creates to a synthetic, collision-proof id-based name right after creation, so within a single
// process a caller-supplied name can only ever match the real, live object from that point on.
// This is a purely action-layer-local fix (the clone's name is never serialized in the first
// place) -- NameResolver/name_resolver.cpp itself, shared Phase 1-7 infrastructure, was not
// touched.
//
// STILL OPEN for RELOADED clones: this fix does not reach clones that already exist in a
// *loaded* pattern file (e.g. tests/actionlayer/fixtures/l_shape_seed.val's own piece-node
// clones from when case 02 first created them) -- confirmed by re-running case 03 against the
// same seed file multiple times and observing a different `basePoint`/`firstPoint` reference id
// each time. Whatever code path in VPattern::Parse() reconstructs a <point type="modeling"> XML
// element back into a live VPointF re-derives its name from the idObject it points to,
// independent of anything this handler does; fixing that is out of scope here (deep, shared XML
// parsing infrastructure -- VPattern lives under src/app/seamly2d/xml/, which this phase's own
// constraints explicitly rule out modifying). The practical impact is limited to which of two
// id-equal-coordinate
// objects gets referenced -- both candidates sit at the exact same (x, y), so every case 03
// render/bounding-box/coordinate result is still deterministic and correct; only the specific
// internal id attribute value in the saved XML can vary run to run. See
// tests/actionlayer/README.md's "Known gaps" section for how this affects golden-file comparison.

// Implements "piece.addPatternPiece": {"name","nodes":["A","B","C","D"],"seamAllowanceWidth",
// "seamAllowance"?,"fill"?,"pieceColor"?} -> {"id","name","op"}. "seamAllowance" (default true)
// toggles VAbstractPiece::hasSeamAllowance() -- separate from the width formula itself, matching
// PatternPieceDialog's own "type a width" + "check the Seams checkbox" split. "nodes" must have
// at least 3 entries, name only
// existing points, and form a closed polygon with no self-intersecting edges -- validated here
// (not left to PatternPieceTool::Create(), which has no such check of its own) and reported as a
// structured {"type":"invalidPiecePath",...} error rather than producing a malformed piece.
// "fill" defaults to FillNone (matching PatternPieceDialog's own default) since a VPiece's raw
// default-constructed fill value crashes PatternPieceTool::RefreshGeometry() -- see this op's
// implementation comment for the root cause. Mirrors PatternPieceTool::Create()'s id+VPiece
// overload.
ActionResult handlePieceAddPatternPiece(const QJsonObject &args, const ActionContext &ctx);

// Implements "piece.addAnchorPoint": {"point","piece"} -> {"id","point","piece","op"}. No scene
// parameter: AnchorPointTool::Create()'s raw overload does not take one. Mirrors
// AnchorPointTool::Create().
ActionResult handlePieceAddAnchorPoint(const QJsonObject &args, const ActionContext &ctx);

// Implements "piece.internalPath": {"piece","nodes":["..."],"lineColor"?,"lineType"?,
// "lineWeight"?,"cutPath"?} -> {"id","piece","op"}. Unlike "piece.addPatternPiece", an internal
// path is not required to be closed, so no polygon-closure/self-intersection check applies here.
// Mirrors InternalPathTool::Create()'s id+VPiecePath overload.
ActionResult handlePieceInternalPath(const QJsonObject &args, const ActionContext &ctx);

// Implements "piece.insertNodes": {"piece","nodes":["..."]} -> {"piece","nodesInserted","op"}.
// PatternPieceTool::insertNodes() is a plain void static method (not a Create() factory, and its
// parameter order is (nodes, pieceId, scene, DATA, DOC) -- reversed from every other tool's
// (..., scene, doc, data, ...) convention, verified against pattern_piece_tool.h before writing
// this handler), so there is no new object id to report back.
//
// KNOWN GAP (documented, not silently shipped): unlike every Create()-based op in this action
// layer, insertNodes() internally pushes a SavePieceOptions QUndoCommand (qApp->getUndoStack()->
// push(...)) rather than mutating data/doc directly. In one reproduction this surfaced a clean,
// catchable VExceptionBadId ("Can't find object Id:") referencing the very node id PrepareNode()
// had just created moments earlier in the same call -- not a crash (this handler's try/catch
// reports it as a normal structured failure, and the batch continues correctly), but its root
// cause inside SavePieceOptions/AddSANode's XML-serialization path was not resolved within this
// investigation's time budget. Flagged as follow-up work.
ActionResult handlePieceInsertNodes(const QJsonObject &args, const ActionContext &ctx);

// Implements "piece.union": {"piece1","piece2","piece1EdgeIndex","piece2EdgeIndex",
// "retainPieces"?} -> {"id","op"}. The two edge-index fields name which edge of each piece's main
// path gets fused (0-based, matching UnionDialog's own edge-selection semantics); this action
// layer does not attempt to auto-detect a shared/co-located edge between the two pieces, since
// UnionTool::Create() itself takes an explicit index pair, not a search. Mirrors UnionTool::Create().
//
// KNOWN GAP (documented, not silently shipped): reproduced a hard segfault inside
// UnionTool::Create() -> unitePieces() -> UnionInitParameters()/createUnion() (union_tool.cpp)
// against two plain rectangular pieces, with both a plausible shared-edge index pair and a
// deliberately-mismatched one (0/0) -- same crash either way, so it is not simply a bad-index
// input this handler's own validation could catch. UnionTool::Create() is the one op in this
// entire phase whose raw-overload code path was not exercisable to a clean success or a clean,
// catchable error within this investigation's time budget; root-causing it further (likely deep
// inside the geometric merge helpers, possibly a GUI-oriented assumption e.g. a QMessageBox call
// on a merge-geometry failure path) is flagged here as explicit follow-up work rather than
// guessed at. Every other op in this file was exercised successfully via a real actiond run.
ActionResult handlePieceUnion(const QJsonObject &args, const ActionContext &ctx);

#endif // PIECE_HANDLERS_H
