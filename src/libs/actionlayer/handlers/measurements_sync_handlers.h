//---------------------------------------------------------------------------------------------------------------------
//  @file   measurements_sync_handlers.h
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

#ifndef MEASUREMENTS_SYNC_HANDLERS_H // Include guard start, prevents this header being processed twice in one translation unit.
#define MEASUREMENTS_SYNC_HANDLERS_H // Marks MEASUREMENTS_SYNC_HANDLERS_H as defined for the remainder of the include guard.

#include "../action_result.h" // Provides ActionResult, the return type every handler below produces.

class QJsonObject;   // Forward declaration; only used by const reference in the signatures below.
class ActionContext; // Forward declaration; only used by const reference in the signatures below.

// Phase 7: swap the pattern's active measurement file and (optionally) recompute geometry against
// it, mirroring MainWindow::updateMeasurements()/syncMeasurements()/checkRequiredMeasurements()
// (mainwindow.cpp) without a MainWindow dependency. Distinct from pattern.listMeasurements
// (pattern_measurements_handler.cpp, Phase 1, read-only) -- these three actions mutate VContainer/doc.
//
// KNOWN GAP (documented, not silently shipped -- Phase 12 undo/redo investigation): none of the
// three actions below push a QUndoCommand. Their mutations go straight to VContainer
// (ClearVariables()+readMeasurements(), the static VContainer::setSize()/setHeight()) and to
// doc->LiteParseTree() -- none of which is undo-tracked, unlike every Create()-based handler's
// AddToCalc/SaveToolOptions push. Phase 12 still opens a QUndoStack macro around each of these
// actions (ActionSchema::mutatesPattern is true for category "measurements", same as every other
// mutating category -- see action_registry.cpp's buildSchema()), so "session.undo"/
// "session.undoStatus" still count one of these as a real step and can label it -- but that step
// is an EMPTY macro: calling "session.undo" to step back past a "measurements.sync" consumes the
// step (the undo-stack index moves) without reverting the measurement values or the size/height
// that were active before it. Only the geometry-creating actions immediately before/after a
// measurement swap (each one its own, separately undoable AddToCalc-backed step) are actually
// affected by undo/redo here; the measurement swap itself is not. Verified by direct code reading
// of VContainer::ClearVariables()/readMeasurements()/setSize()/setHeight() and doc->
// LiteParseTree() -- none constructs or pushes a QUndoCommand anywhere in this call chain.
// Restoring pre-swap measurement state after an undo would require its own dedicated undo command
// (e.g. snapshotting/restoring VContainer's measurement variables), which is out of scope for this
// phase; flagged here as follow-up work rather than silently assumed to "just work" like the
// Create()-based ops.

// Implements "measurements.load": {"path","size"?,"height"?} -> {"success","measurementsLoaded",
// "type"} or a structured failure. Loads and validates the named measurement file exactly as
// MainWindow::openMeasurementFile()+updateMeasurements() do (unknown-format check, schema
// upgrade, known-name validation, multisize-inches rejection, checkRequiredMeasurements()), then
// -- only if every check passes -- clears and repopulates VContainer's measurement variables and
// sets the active size/height. Does NOT recompute pattern geometry; call "measurements.recompute"
// (or use "measurements.sync") for that, exactly as the real updateMeasurements() never reparses
// on its own either -- syncMeasurements() is the one that chains a LiteParseTree() call after it.
ActionResult handleMeasurementsLoad(const QJsonObject &args, const ActionContext &ctx); // Implemented in measurements_sync_handlers.cpp.

// Implements "measurements.recompute": {} -> {"success"} or a structured failure. Calls
// doc->LiteParseTree(Document::LiteParse), the same call MainWindow::syncMeasurements() itself
// makes after a successful updateMeasurements() (mainwindow.cpp). Generically useful standalone --
// not just after a measurement change -- since it is simply "re-evaluate every formula in the
// pattern against VContainer's current variables and rebuild geometry".
ActionResult handleMeasurementsRecompute(const QJsonObject &args, const ActionContext &ctx); // Implemented in measurements_sync_handlers.cpp.

// Implements "measurements.sync": {"path","size"?,"height"?} -> {"success","measurementsLoaded",
// "type","recomputed"} or a structured failure. Convenience action equivalent to
// "measurements.load" immediately followed by "measurements.recompute" in the same batch --
// internally shares the same private load/recompute helpers those two ops call, rather than
// dispatching back through ActionRegistry, so it is a direct composition, not a second
// implementation that could silently drift from the two standalone ops.
ActionResult handleMeasurementsSync(const QJsonObject &args, const ActionContext &ctx); // Implemented in measurements_sync_handlers.cpp.

#endif // MEASUREMENTS_SYNC_HANDLERS_H // End of include guard started above.
