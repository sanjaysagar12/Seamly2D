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
// FOUND AND FIXED -- NameResolver/piece-node-clone name collision (found while validating
// tests/actionlayer/cases/03_import_l_shape_to_rectangle as genuine run-to-run non-determinism:
// the same actions.json resolved "basePoint": "B" to a different object id across separate
// process runs of the identical build -- and, worse, one of those resolutions produced a saved
// <calculation> element referencing an id only ever *defined* later, in <modeling>, which then
// failed to reopen in Seamly2D's own document-order parser with "ExceptionBadId: Can't find
// object Id: , id = 14"). Root cause: VAbstractTool::CreateNode<VPointF>() copy-constructs each
// piece node's clone from its source point, which also copies the source's name() verbatim (even
// though the clone's serialized XML <point type="modeling"> element never carries a "name"
// attribute -- only NameResolver's in-memory lookup ever reads it) -- and, independently,
// VPattern::ParseNodePoint() (src/app/seamly2d/xml/vpattern.cpp) does the exact same thing when
// *reloading* a saved piece, so the collision reappears on every load, not just within the
// process that first created the piece. Before this fix, NameResolver::idForName()
// (name_resolver.cpp) was a plain linear scan over VContainer::DataGObjects() with no preference
// between a "calculation" object and a "modeling" clone that happen to share a name, so it could
// resolve to either one, depending on QHash's per-process randomized iteration order.
//
// THE ACTUAL FIX is NameResolver::idForName()'s scoped (Draw requiredMode) overload -- see
// name_resolver.h's own comment on it -- now used by every calculation-context handler in this
// action layer (line_handlers.cpp, formula_point_handlers.cpp, curve_handlers.cpp,
// cutpoint_handlers.cpp, operation_handlers.cpp, piece_handlers.cpp, render_handlers.cpp's
// "highlight" resolution). It filters on VGObject::getMode() before ever comparing names, so it
// is architecturally unable to match a Draw::Modeling clone when Draw::Calculation was requested
// -- regardless of whether that clone was created moments ago in this same process or
// reconstructed by reloading a file, which is what makes this fix complete for both cases
// (verified: cases/03_import_l_shape_to_rectangle now produces byte-identical output across
// repeated runs, and its saved rectangle.val was confirmed to reload cleanly through actiond's own
// real VPattern::Parse() path -- the same parser Seamly2D's interactive GUI uses -- with no error).
//
// resolvePreparedPointNodes() below still also renames every clone it creates (a second,
// independent safety net predating the scoped overload -- see that rename's own comment for why
// it is kept rather than removed now that it is no longer load-bearing on its own).
//
// FOUND AND FIXED -- cross-draft-block dangling reference (found while investigating a saved
// pattern that failed to reopen in the real, unmodified Seamly2D GUI with "ExceptionBadId: Can't
// find tool in table., id = 17" -- an in-session actiond run of the same script reported no error
// at all, so this was invisible to every in-process check, the same class of "works live, corrupts
// on reload" bug as the earlier SetMPath measurement-path fix). Root cause, traced to
// VPattern::parseDraftBlockElement() (src/app/seamly2d/xml/vpattern.cpp):
//
//     case 0: // TagCalculation
//         data->ClearCalculationGObjects();   // wipes EVERY calculation-scope object from EVERY
//                                              // previously-parsed draft block, unconditionally
//         ParseDraftStage(domElement, parse, Draw::Calculation);
//
// Every <draftBlock>'s own <calculation> section, when (re)parsed, wipes all calculation-scope
// points/lines/curves loaded from every *other* draft block already parsed so far -- confirmed in
// VContainer::ClearCalculationGObjects() itself, which unconditionally clears every object with
// getMode() == Draw::Calculation regardless of which draft block it came from. This is correct,
// intentional, load-bearing behavior (draft blocks are self-contained calculation scopes, not
// meant to interlink -- see basePoint's own handler comment on why VToolBasePoint::AddToFile()
// unconditionally starts a brand-new block with no merge-into-existing-block logic), not something
// this fix touches or works around.
//
// The bug: nothing in this file previously checked that a "nodes" (or "point") argument's
// resolved id was created in the SAME draft block as the pattern's own currently-active one
// (VAbstractPattern::getActiveDraftBlockName()) -- so referencing a point from a *different*,
// earlier draft block succeeded silently in the live session (VContainer is never wiped
// mid-session, only during file *parsing*), producing a piece/anchor whose modeling clone
// referenced an idObject that belongs to a different block. On reload, whichever draft block's
// <calculation> section parses LAST wipes every earlier block's points first -- so by the time
// that earlier block's own <modeling> clone is reached, VPattern::ParseNodePoint()'s
// data->GeometricObject<VPointF>(idObject) lookup throws VExceptionBadId, which is caught and
// SILENTLY SKIPPED ("Possible case. Parent was deleted, but the node object is still here." --
// a legitimate case for an actually-deleted parent, but a false positive here) -- so the clone is
// never created, and the piece's later reference to it fails with exactly the reported error.
//
// THE FIX: checkSameDraftBlock() (piece_handlers.cpp), consulted by resolvePointNodes()/
// resolvePreparedPointNodes() (both "nodes"-array resolvers, shared by every op below that has
// one) and inline by handlePieceAddAnchorPoint() (a single point, not a "nodes" array). Resolves
// each referenced id's OWN draft block via a doc->getHistory() scan (VToolRecord::
// getDraftBlockName() -- the same field VAbstractTool::AddRecord() already stamps every tool's
// history entry with, using whatever getActiveDraftBlockName() was at that tool's own creation
// time) and rejects the whole action with a structured "crossDraftBlockReference" error the
// moment any id's block doesn't match the active one -- BEFORE any DOM element or modeling clone
// is created from it, not after (resolvePreparedPointNodes() was restructured into three explicit
// phases -- resolve, validate, clone -- specifically so a validation failure never leaves a
// dangling, unreferenced clone behind; see that function's own comment for why the earlier
// single-loop version could not make that guarantee).
//
// KNOWN GAP (documented, not silently unhandled): an operation-created destination point (move/
// rotation/mirrorByLine/mirrorByAxis) has no individual doc->getHistory() entry of its own -- only
// the *operation* tool's own id does (confirmed by reading each of those four tools' own
// AddRecord() call, which always passes the operation's own id) -- so such a point's own draft
// block cannot be determined this way, and checkSameDraftBlock() treats that case as
// "unverifiable," not as a violation. A cross-draft-block reference through an operation result
// specifically would not be caught by this fix. Flagged as a real, narrow, tracked follow-up gap,
// not silently left for someone to rediscover.
//
// piece.union was checked and confirmed NOT exposed to this same failure class: it never
// references raw calculation-scope points at all, only two already-existing pieces (by name/id),
// each already fully self-contained with its own previously-validated modeling clones -- uniting
// them creates no new clone referencing a calculation-scope point, so there is nothing here for a
// draft-block mismatch to corrupt. (Separately, and unrelated to this fix, piece.union has its own
// pre-existing, already-documented KNOWN GAP -- a reproducible segfault -- see its own doc comment
// below; that gap is not touched or affected by this fix either.)

// Implements "piece.addPatternPiece": {"name","nodes":["A","B","C","D"],"seamAllowanceWidth",
// "seamAllowance"?,"fill"?,"pieceColor"?,"mx"?,"my"?,"createGroup"?,"groupName"?} ->
// {"id","name","mx","my","op","group"?,"groupError"?} -- "mx"/"my" in the response are always the
// position actually used (the caller's own explicit values, or PieceLayoutCursor's auto-placed
// ones when both were omitted), never just an echo of what was (or wasn't) given. "seamAllowance" (default true)
// toggles VAbstractPiece::hasSeamAllowance() -- separate from the width formula itself, matching
// PatternPieceDialog's own "type a width" + "check the Seams checkbox" split. "nodes" must have
// at least 3 entries, name only
// existing points, and form a closed polygon with no self-intersecting edges -- validated here
// (not left to PatternPieceTool::Create(), which has no such check of its own) and reported as a
// structured {"type":"invalidPiecePath",...} error rather than producing a malformed piece. Every
// referenced node must also belong to the pattern's currently active draft block (see this file's
// own "FOUND AND FIXED -- cross-draft-block dangling reference" module comment) -- a mismatch is
// reported as a structured {"type":"crossDraftBlockReference","offendingNodes","expectedDraftBlock",
// "foundDraftBlock",...} error, checked before any DOM element or modeling clone is created.
// "fill" defaults to FillNone (matching PatternPieceDialog's own default) since a VPiece's raw
// default-constructed fill value crashes PatternPieceTool::RefreshGeometry() -- see this op's
// implementation comment for the root cause. Mirrors PatternPieceTool::Create()'s id+VPiece
// overload.
//
// "mx"/"my" (both optional; a real, previously-missing bug fix, not a new feature -- see this op's
// own implementation comment): PatternPieceTool::Create()/RefreshGeometry() never assigns a piece
// position beyond VPiece's default-constructed (0,0), the same way the interactive GUI itself
// leaves a human to drag each new piece to a clear spot afterward -- with no such step available to
// a headless caller, every piece built with no explicit "mx"/"my" used to land at the identical
// position, overlapping every other piece built the same way. Giving EITHER "mx" OR "my" (even just
// one) always takes that literal value (the other defaulting to 0) with no auto-placement involved
// at all; omitting BOTH triggers this session's PieceLayoutCursor (ActionContext::
// pieceLayoutCursor(), piece_layout_cursor.h) to place the piece clear of every other piece this
// same cursor has already placed -- a simple non-overlapping left-to-right "shelf" layout, not a
// real cutting-layout optimization.
//
// "createGroup" (optional, default false -- an explicit opt-in convenience, never something that
// "should" have happened automatically; the real GUI's own PatternPieceTool::Create() creates no
// group either) creates a group containing the piece's own original node points (named "groupName"
// if given, else this piece's own "name") by calling handleGroup() (operation_handlers.h) directly
// -- reused, not reimplemented. A group-name collision (or any other handleGroup() failure) is
// reported via the success payload's "groupError" field rather than failing this whole action: the
// piece itself has already been created successfully by that point.
ActionResult handlePieceAddPatternPiece(const QJsonObject &args, const ActionContext &ctx);

// Implements "piece.addAnchorPoint": {"point","piece"} -> {"id","point","piece","op"}. No scene
// parameter: AnchorPointTool::Create()'s raw overload does not take one. Mirrors
// AnchorPointTool::Create(). "point" must belong to the pattern's currently active draft block --
// see this file's own "FOUND AND FIXED -- cross-draft-block dangling reference" module comment;
// AnchorPointTool::Create() clones "point" internally the same way piece.addPatternPiece's own
// node resolution does, so it is exposed to the exact same failure mode.
ActionResult handlePieceAddAnchorPoint(const QJsonObject &args, const ActionContext &ctx);

// Implements "piece.internalPath": {"piece","nodes":["..."],"lineColor"?,"lineType"?,
// "lineWeight"?,"cutPath"?} -> {"id","piece","op"}. Unlike "piece.addPatternPiece", an internal
// path is not required to be closed, so no polygon-closure/self-intersection check applies here.
// Mirrors InternalPathTool::Create()'s id+VPiecePath overload. Same cross-draft-block "nodes"
// validation as "piece.addPatternPiece" (shared resolvePreparedPointNodes()) applies here too.
ActionResult handlePieceInternalPath(const QJsonObject &args, const ActionContext &ctx);

// Implements "piece.insertNodes": {"piece","nodes":["..."]} -> {"piece","nodesInserted","op"}.
// PatternPieceTool::insertNodes() is a plain void static method (not a Create() factory, and its
// parameter order is (nodes, pieceId, scene, DATA, DOC) -- reversed from every other tool's
// (..., scene, doc, data, ...) convention, verified against pattern_piece_tool.h before writing
// this handler), so there is no new object id to report back. Same cross-draft-block "nodes"
// validation as "piece.addPatternPiece" (shared resolvePointNodes()) applies here too -- see this
// file's own "FOUND AND FIXED -- cross-draft-block dangling reference" module comment.
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
