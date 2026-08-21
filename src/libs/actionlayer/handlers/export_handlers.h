//---------------------------------------------------------------------------------------------------------------------
//  @file   export_handlers.h
//  @author Seamly2D Contributors
//  @date   21 Aug, 2026
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

#ifndef EXPORT_HANDLERS_H // Include guard start, prevents this header being processed twice in one translation unit.
#define EXPORT_HANDLERS_H // Marks EXPORT_HANDLERS_H as defined for the remainder of the include guard.

#include "../action_result.h" // Provides ActionResult, this handler's return type.

class QJsonObject;   // Forward declaration; only used by const reference in the signature below.
class ActionContext; // Forward declaration; only used by const reference in the signature below.

// Implements the "export.scene" op: writes the current draft scene (ctx.scene(), the same single
// scene render.snapshot rasterizes -- ActionContext exposes no piece scene to this op yet) to one
// of the vector (SVG/PDF/PS/EPS), flat-DXF (nine AutoCAD versions), or raster (PNG/JPG/BMP/PPM/TIF)
// formats a caller requests. This is Phase A of the action-layer export effort: direct scene
// export, with no piece nesting/cutting-layout arrangement -- see docs/export-actions-notes.md for
// the full phase breakdown and the architecture decision behind why this handler calls the same
// low-level, non-GUI writers Seamly2D's own MainWindowsNoGUI export functions do (SvgGenerator-
// equivalent QSvgGenerator usage, QPrinter, VDxfPaintDevice) directly, rather than linking against
// MainWindow/MainWindowsNoGUI/ExportLayoutDialog.
ActionResult handleExportScene(const QJsonObject &args, const ActionContext &ctx); // Implemented in export_handlers.cpp.

#endif // EXPORT_HANDLERS_H // End of include guard started above.
