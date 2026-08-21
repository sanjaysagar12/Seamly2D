//---------------------------------------------------------------------------------------------------------------------
//  @file   session_undo_handlers.h
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

#ifndef SESSION_UNDO_HANDLERS_H // Include guard start, prevents this header being processed twice in one translation unit.
#define SESSION_UNDO_HANDLERS_H // Marks SESSION_UNDO_HANDLERS_H as defined for the remainder of the include guard.

#include "../action_result.h" // Provides ActionResult, the return type every handler below produces.

class QJsonObject;   // Forward declaration; only used by const reference in the signatures below.
class ActionContext; // Forward declaration; only used by const reference in the signatures below (unused by every handler's own body -- see each .cpp definition).

// Phase 12: session.undo/session.redo/session.undoStatus operate directly on qApp->getUndoStack()
// (the same process-lifetime QUndoStack every mutating Create()/SaveOption() call already pushes
// onto -- see actiond_application.cpp's own comment on why that stack exists at all), not through
// ActionContext -- ActionContext bundles the scene/doc/data pointers handlers read/mutate the
// *pattern* through, and the undo stack is qApp-scoped, not pattern-scoped, exactly like every
// other handler in this action layer that already calls qApp->... directly (e.g. point_edit_
// handlers.cpp's qApp->patternUnit(), piece_handlers.cpp's qApp->toPixel()). None of these three
// ops is itself macro-wrapped by ActionEngine::run() (see action_registry.cpp's own comment on
// why "session" is one of the two categories ActionSchema::mutatesPattern excludes) -- wrapping
// the undo/redo mechanism in its own undo macro would be circular and meaningless.
//
// Cross-session undo (undoing something pushed by a *previous* actiond process) is out of scope:
// qApp->getUndoStack() is a plain in-memory QUndoStack, valid only for this process's lifetime,
// matching how the interactive GUI's own undo already works (it does not persist across restarts
// either). Object deletion ("point.delete"/"tool.delete") remains a separate, already-documented
// gap (see the Phase 9 changelog entry) that this phase does not add as a side effect.
//
// ============================================================================================
// CRITICAL, VERIFIED FINDING -- READ BEFORE RELYING ON "session.undo"/"session.redo":
// ============================================================================================
// "session.undo" correctly rewrites the pattern DOM (the tool's <point>/<line>/... element is
// genuinely removed from the <calculation> tree -- AddToCalc::undo(), addtocalc.cpp -- and a
// "session.save" issued right after DOES write a smaller, correctly-reverted .val file). It does
// NOT, however, remove the corresponding object from the live in-memory VContainer or scene:
// there is no object-deletion mechanism anywhere in this codebase yet (this is the same
// already-documented Phase 9 gap -- "no delete ... action existed yet for any object type" --
// just newly visible here, not a Phase 12 regression). Every already-alive tool's own
// FullUpdateFromFile() override does re-run synchronously on undo (see pattern_session.cpp's own
// comment on exactly why that part works with no Qt event loop running), but it refreshes a
// surviving tool's geometry from its DOM element -- it does not, and structurally cannot, delete a
// tool whose element just disappeared: VDrawTool::ReadAttributes() (vdrawtool.cpp) finds nothing,
// logs "Can't find tool with id", and simply returns, leaving the stale object exactly as it was.
//
// Reproduced directly via `actiond --pattern ... --actions ...`: create basePoint "A" + endLine
// "B" + line(A,B); "session.undo" with count 3 (canUndo becomes false); "pattern.dump" STILL
// reports both points and the full history, unchanged; "render.snapshot" still renders the full
// geometry; "pattern.resolveName" for "A" still resolves to the original id. Creating a brand new
// basePoint also named "A" afterward SUCCEEDS (no "already exists" rejection -- the stale object
// and the fresh one now silently coexist, both in Draw::Calculation scope); the very next action
// that references "A" by name then fails with a structured {"kind":"duplicate",...} nameResolution
// error, because NameResolver::idForName() now finds two live matches. See
// tests/actionlayer/scripts/10_undo_redo.json's own "_description" for the same reproduction
// preserved as a golden-file regression case.
//
// PRACTICAL IMPLICATION: "session.undo" is reliable for controlling what a subsequent
// "session.save" writes to disk. It is NOT currently reliable for "make the live pattern look like
// it did before" from the perspective of "pattern.dump"/"render.snapshot"/"export.scene"/any
// later action's name resolution -- those all keep seeing the undone objects until the process
// re-loads the saved file fresh (a new PatternSession). Fixing this properly would mean giving
// every mutating tool a real delete/prune step invoked from FullUpdateFromFile() (or an
// equivalent full VPattern::Parse(Document::FullParse) re-run against the now-current DOM) --
// out of scope for this phase; flagged here as the primary follow-up this investigation surfaced,
// not silently worked around or hidden behind an optimistic docstring.
//
// IMPORTANT SCOPE REFINEMENT (also reproduced directly, see tests/actionlayer/scripts/
// 10_undo_redo.json): the staleness above is specific to undoing an object's CREATION --
// AddToCalc-backed (every basePoint/line/endLine/curve/cut-point/operation/... op) or
// AddPiece/AddImage-backed. Undoing an in-place EDIT of an already-existing object --
// "point.edit", backed by SaveToolOptions -- is NOT affected and works completely correctly,
// including the recomputed geometry (x/y), not just the formula string: the edited tool's own
// DOM element is not removed, only its attributes are rewritten back, so ReadAttributes()->
// RefreshGeometry() (VDrawTool::FullUpdateFromFile()'s underlying call chain) successfully
// re-reads it and the live object updates in place. Reproduced: endLine "C" (length "50", angle
// "90"), then point.edit(C, length "60", angle "45") -- two SaveToolOptions pushes grouped into
// one macro (see action_engine.cpp's own comment on this exact scenario) -- then one
// "session.undo" call correctly restored "pattern.dump"'s reported x/y for "C" to its
// pre-point.edit value, byte-for-byte. Only "undoing a creation" leaves a stale live object; "undoing
// an edit" does not.
// ============================================================================================

// Implements "session.undo": {"count"?} -> {"undone","canUndo","canRedo","index","count"}.
// "count" (default 1) is how many times to call QUndoStack::undo(); stops early (without erroring)
// once QUndoStack::canUndo() goes false, so calling this with the stack already empty -- or with a
// "count" larger than how many steps remain -- is a normal, successful no-op/partial result, never
// a structured failure: there is nothing wrong with an AI caller asking to undo more than is left.
// "index"/"count" mirror QUndoStack::index()/count() after the call, for a caller that wants to
// keep its own running position without a separate "session.undoStatus" round trip.
ActionResult handleSessionUndo(const QJsonObject &args, const ActionContext &ctx); // Implemented in session_undo_handlers.cpp.

// Implements "session.redo": {"count"?} -> {"redone","canUndo","canRedo","index","count"}. Exact
// mirror of "session.undo" using QUndoStack::redo()/canRedo() -- see that op's own comment above
// for the shared "stop early, never error on running out" contract.
ActionResult handleSessionRedo(const QJsonObject &args, const ActionContext &ctx); // Implemented in session_undo_handlers.cpp.

// Implements "session.undoStatus": {} -> {"canUndo","canRedo","index","count","labels"}. Read-only
// introspection over qApp->getUndoStack() -- never itself macro-wrapped (see this file's own
// header comment) and never mutates anything. "labels" is a bounded window (5 entries before the
// current index, 5 after -- not the whole stack unconditionally, which could be arbitrarily large
// after a long-running daemon session) of {"index","label","applied"}, where "label" is
// QUndoStack::text(i) (the macro label "session.undo"/"session.redo" would act on, built by
// action_engine.cpp's describeActionForMacroLabel() when that step was originally pushed) and
// "applied" is true for a step before the current index (i.e. currently in effect -- the next
// "session.undo" would revert the one immediately before the index) and false for one at or after
// it (currently reverted/never redone -- the next "session.redo" would reapply the one at the
// index). Intended for an AI caller to sanity-check *what* it is about to undo/redo before calling
// either action for real -- though see this file's own "CRITICAL, VERIFIED FINDING" section above
// for the limits of what "undone"/"applied" actually means for live pattern state, not just the
// undo-stack bookkeeping itself.
ActionResult handleSessionUndoStatus(const QJsonObject &args, const ActionContext &ctx); // Implemented in session_undo_handlers.cpp.

#endif // SESSION_UNDO_HANDLERS_H // End of include guard started above.
