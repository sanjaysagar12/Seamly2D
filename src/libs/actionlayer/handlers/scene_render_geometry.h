//---------------------------------------------------------------------------------------------------------------------
//  @file   scene_render_geometry.h
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

#ifndef SCENE_RENDER_GEOMETRY_H // Include guard start, prevents this header being processed twice in one translation unit.
#define SCENE_RENDER_GEOMETRY_H // Marks SCENE_RENDER_GEOMETRY_H as defined for the remainder of the include guard.

#include <QRectF> // Provides QRectF, the type of the computed sourceRect member.

class QJsonObject;       // Forward declaration; only used by const reference in the function signature below.
class VMainGraphicsScene; // Forward declaration; only used by pointer in the function signature below.

// Result of computeSceneRenderGeometry(): the padded scene-space content bounds ("sourceRect") and
// the device size ("pixelWidth"/"pixelHeight") a render/export handler should target. Shared by
// render.snapshot (render_handlers.cpp, raster-only) and export.scene (export_handlers.cpp, every
// format) so both derive identical padding/sizing behavior from the same code instead of two
// independently-maintained copies.
struct SceneRenderGeometry
{
    // False only when the scene has no content at all (scene->items().isEmpty()); every other
    // field is left default-constructed in that case, and the caller is expected to report a
    // structured "empty scene" failure rather than proceed.
    bool ok = false;

    QRectF sourceRect;  // Padded content bounding box, in scene coordinates -- the region a caller's "highlight"/render/export logic should treat as "the whole drawing".
    int pixelWidth = 0;  // Derived/explicit device width (raster: pixels; vector/DXF: scene units used as the device's own unit).
    int pixelHeight = 0; // Derived/explicit device height. See pixelWidth.
};

// Derives sourceRect/pixelWidth/pixelHeight from a scene and a handler's own "padding"/"width"/
// "height" args, exactly as render.snapshot originally computed them inline: itemsBoundingRect()
// (not sceneRect(), which reflects the editor's configured canvas rather than actual content)
// expanded by "padding" on every side, then sized either verbatim (both width and height given,
// even if that distorts the aspect ratio -- the caller's explicit choice), derived from the other
// given dimension's aspect ratio (only one of width/height given), or (neither given) mapped 1:1
// from scene units to device units.
//
// applyRasterCap selects whether the "neither width nor height given" 1:1 branch also clamps the
// larger dimension to 4096 (render.snapshot's raster-memory safety cap, meaningless for a vector/
// DXF device that never allocates a pixel buffer) -- pass true only for a raster (QImage) target.
SceneRenderGeometry computeSceneRenderGeometry(VMainGraphicsScene *scene, const QJsonObject &args, bool applyRasterCap); // Implemented in scene_render_geometry.cpp.

// Same padding/aspect-ratio/raster-cap derivation as computeSceneRenderGeometry() above (which is
// now a thin wrapper around this, using scene->itemsBoundingRect() as itemsRect), but starting from
// an already-known scene-space rect instead of a whole scene's items. Added so render.snapshot's
// "target": "piece" support (render_handlers.cpp) can crop to one piece's own graphics item's
// sceneBoundingRect() -- a single item's bounds within ctx.pieceScene(), not that scene's full
// itemsBoundingRect(), which would include every other assembled piece too -- while still sharing
// the exact same sizing math as the "draft"-target/export.scene paths, instead of a third
// independently-maintained copy of it. Always reports ok=true: unlike a whole scene, a resolved
// graphics item's own bounding rect is never "empty" in the "nothing to render" sense
// computeSceneRenderGeometry()'s scene->items().isEmpty() guards against; a genuinely
// zero-area item (e.g. a perfectly degenerate rect) still produces a valid, if visually trivial,
// crop rather than a meaningful error.
SceneRenderGeometry computeRenderGeometryForRect(const QRectF &itemsRect, const QJsonObject &args, bool applyRasterCap); // Implemented in scene_render_geometry.cpp.

#endif // SCENE_RENDER_GEOMETRY_H // End of include guard started above.
