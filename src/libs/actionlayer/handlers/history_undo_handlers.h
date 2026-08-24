//---------------------------------------------------------------------------------------------------------------------
//  @file   history_undo_handlers.h
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

#ifndef HISTORY_UNDO_HANDLERS_H // Include guard start, prevents this header being processed twice in one translation unit.
#define HISTORY_UNDO_HANDLERS_H // Marks HISTORY_UNDO_HANDLERS_H as defined for the remainder of the include guard.

#include "../action_result.h" // Provides ActionResult, the return type this handler produces.

class QJsonObject;   // Forward declaration; only used by const reference in the signature below.
class ActionContext; // Forward declaration; only used by const reference in the signature below.

// Phase 12 (second design; supersedes and replaces the abandoned QUndoStack-based
// session.undo/session.redo/session.undoStatus -- see CHANGELOG.md's own "Phase 12" entry for why
// that design was rejected: a QUndoCommand holds live pointers into one process's in-memory
// VPattern/VContainer, so it cannot survive session.save -> process exit -> a new process
// reloading the file, which defeats the entire point of an AI caller working across separate
// actiond invocations against the same saved pattern).
//
// "pattern.undo" instead works directly at the DOM/history level, one-directional (no redo, no
// QUndoStack involvement at all), so its effect is exactly what gets saved by a subsequent
// "session.save" -- there is no live-vs-saved distinction to get wrong here, unlike the abandoned
// design's own headline finding.
//
// THE SAFETY PROPERTY THIS RELIES ON: Seamly2D's pattern format only lets a formula reference an
// object that already existed at the time the referencing tool was created (the whole DOM is
// built by successive Create() calls, each only able to name previously-registered ids/names).
// Consequently, undoing history entries in strict reverse-chronological order (most-recently-
// created first) can never hit an object something else still depends on -- nothing can depend on
// the most-recently-created entry, by definition. This is what makes this handler safe with *no*
// reference-count/dependency check of its own: do not add one as a defensive substitute for this
// ordering guarantee -- enforce the ordering instead (this handler always walks
// doc->getHistory()'s tail backward, one entry at a time, never out of order).
//
// WHICH DELETE-COMMAND CLASS REVERSES WHICH ENTRY (verified against current develop by reading
// every deleteTool()/Remove() override that pushes one): the overwhelming majority of
// VToolRecord entries (every point/line/curve/cut-point/operation, plus modeling-scope entries
// like InternalPath/AnchorPoint/NodePoint) use the base VAbstractTool::deleteTool()'s own command,
// DelTool -- this handler's default case. Tool::BasePoint is the one point-creating tool that
// overrides deleteTool() to use DeleteDraftBlock instead (VToolBasePoint::deleteTool(),
// vtoolbasepoint.cpp) -- because a basePoint action doesn't just create a point, it creates a
// whole new <draftBlock>; reversing it needs to remove that whole block, not one <point> node
// inside it (which DelTool alone would leave as an orphaned empty draft block). Tool::Piece is the
// one tool that overrides deleteTool() to use DeletePiece instead (PatternPieceTool::deleteTool(),
// pattern_piece_tool.cpp) -- needing the piece's own VPiece value, not just its id, per
// DeletePiece's constructor.
//
// KNOWN GAP (documented, not silently under-covered): "group" (operation_handlers.cpp's
// handleGroup()) builds its DOM element directly (doc->createGroup()/parseGroups()) without ever
// calling VAbstractTool::AddRecord() -- so a group's creation has NO entry in doc->getHistory()
// at all, and "pattern.undo" has no way to reach it (there is nothing to walk past or skip; it is
// simply invisible to this op). Same for "piece.insertNodes" (PatternPieceTool::insertNodes() is a
// plain static method, not a Create() factory -- no AddRecord() call either). Both are
// pre-existing gaps in what doc->getHistory() itself tracks, not something this handler
// introduces or could special-case its way around; an AI caller mixing these ops into a batch it
// expects to be fully undoable by count should be aware "pattern.undo {"count": N}" will skip
// straight past their effect, silently undoing an *older* entry instead once N reaches that deep --
// flagged here at maximum visibility rather than left to be rediscovered by a confusing dump diff.
// This gap got materially more likely to bite once "piece.addPatternPiece"'s own "createGroup"
// option (piece_handlers.cpp) defaulted to true: undoing such a piece via "pattern.undo" reverses
// the Tool::Piece entry (DeletePiece, above) but leaves that piece's own auto-created group behind
// untouched, now referencing node ids the just-undone piece owned -- an orphaned group, not a
// crash (nothing dereferences a dangling group member eagerly), but a real, visible discrepancy in
// Group Manager after an undo. Not fixed here: doing so would mean either giving "group" its own
// history entry (a real fix, but out of this scope) or having "pattern.undo" special-case
// Tool::Piece to also remove any group it happens to have created, which would entangle two
// otherwise-independent ops. Documented as a known follow-up, same as the two gaps above.
//
// WHY THIS HANDLER CANNOT, BY ITSELF, MAKE THE DELETION VISIBLE TO "pattern.dump"/
// "render.snapshot"/name resolution: DelTool/DeletePiece/DeleteDraftBlock's own redo() methods
// only remove the DOM element and emit NeedFullParsing()/FullUpdateFromFile() -- signals this
// action layer's investigation (see CHANGELOG.md's prior "Phase 12" entry, and pattern_session.cpp's
// own comment) already found do NOT prune VContainer on their own; only a genuine
// VPattern::Parse(Document::FullParse) does that (confirmed by reading VPattern::PrepareForParse():
// only the FullParse branch calls data->ClearForFullParse()/clearHistory()/draftScene->clear()).
// VAbstractPattern::LiteParseTree() -- the one reparse entry point reachable from actionlayer's own
// VAbstractPattern* handles -- explicitly refuses Document::FullParse ("Lite parsing doesn't
// support full parsing"). Only VPattern::Parse() (declared on VPattern, not VAbstractPattern; see
// measurements_sync_handlers.cpp's own comment on why actionlayer.pro deliberately never links
// vpattern.* directly) can do a real FullParse -- and only PatternSession (src/app/actiond/), which
// owns the real VPattern instance, can reach it. So THIS handler only performs the DOM-level
// deletions and prunes its own copy of doc->getHistory() as it goes (so its own loop doesn't
// revisit an entry it already removed); PatternSession::runActions() is the one place that
// notices a successful "pattern.undo" occurred and re-runs a full VPattern::Parse(Document::
// FullParse) afterward -- exactly the same call its constructor already makes for a freshly loaded
// file -- to make the deletions actually visible everywhere else. See pattern_session.cpp's own
// comment on that step for the full reasoning; this split is a hard architectural constraint
// (the VAbstractPattern/VPattern boundary), not a design choice made for its own sake.
//
// Implements "pattern.undo": {"count"?} -> {"undone","remaining","entries":[{"id","kind"},...]}.
// "count" (default 1) is how many history entries to remove, most-recently-created first; stops
// early (without erroring) once history is exhausted, so a "count" larger than what remains is a
// normal, successful partial result, never a structured failure -- matching the exact
// "empty/exhausted history is not an error" contract session.undo's own earlier (abandoned) design
// already established, kept here because it's still the right contract for this handler's own
// "count" parameter, independent of which mechanism ended up implementing undo.
// "entries" lists exactly what was removed, in removal order (most-recent first), each with its id
// and "kind" ("tool", "piece", or "draftBlock" -- see above for which is which), so a caller can
// log/verify without a separate "pattern.dump" round trip (still recommended for real
// verification, since "kind"/"id" alone don't show the resulting geometry).
ActionResult handlePatternUndo(const QJsonObject &args, const ActionContext &ctx); // Implemented in history_undo_handlers.cpp.

#endif // HISTORY_UNDO_HANDLERS_H // End of include guard started above.
