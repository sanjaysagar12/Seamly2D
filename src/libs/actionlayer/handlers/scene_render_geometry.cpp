//---------------------------------------------------------------------------------------------------------------------
//  @file   scene_render_geometry.cpp
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

#include "scene_render_geometry.h" // Brings in the SceneRenderGeometry struct and this function's declaration.

#include "../../vwidgets/vmaingraphicsscene.h" // Brings in the full VMainGraphicsScene definition so items()/itemsBoundingRect() are callable.

#include <QJsonObject> // Provides QJsonObject, the type of the "args" parameter this function reads from.
#include <qglobal.h>   // Provides qMax/qRound, used throughout the sizing derivation below.

// See scene_render_geometry.h for the full behavioral contract; this is render_handlers.cpp's
// original render.snapshot sizing logic, extracted verbatim so export_handlers.cpp can reuse it
// exactly rather than re-deriving (or subtly diverging from) the same padding/aspect-ratio rules.
SceneRenderGeometry computeSceneRenderGeometry(VMainGraphicsScene *scene, const QJsonObject &args, bool applyRasterCap)
{
    SceneRenderGeometry result; // Default-constructed: ok=false, until every check below passes.

    // items().isEmpty() (rather than itemsBoundingRect().isEmpty()) is the correct "truly nothing
    // to render" check: a single perfectly horizontal or vertical line item has a bounding rect
    // with zero width or height -- and QRectF::isEmpty() would then (wrongly) report the whole
    // scene as empty even though it has real content to draw.
    if (scene->items().isEmpty()) // No items at all: nothing meaningful can be rendered.
    {
        return result; // ok stays false; caller reports "empty scene" itself with its own op-specific wording.
    }

    const qreal padding = args.value(QStringLiteral("padding")).toDouble(20.0); // Margin added around the content on every side.

    const QRectF itemsRect = scene->itemsBoundingRect(); // Tight bounds of everything currently drawn, in scene coordinates.
    // itemsBoundingRect() (not sceneRect()) is used deliberately: sceneRect() reflects the
    // editor's configured/scrollable canvas size, which is usually much larger than the drawn
    // content and would produce a mostly-blank result; itemsBoundingRect() crops to what's actually there.
    const QRectF sourceRect = itemsRect.adjusted(-padding, -padding, padding, padding); // Expand by padding on all four sides.

    const bool hasWidth = args.contains(QStringLiteral("width"));   // Caller supplied an explicit width.
    const bool hasHeight = args.contains(QStringLiteral("height")); // Caller supplied an explicit height.

    int pixelWidth = 0;  // Populated by exactly one of the three branches below.
    int pixelHeight = 0; // Populated by exactly one of the three branches below.

    if (hasWidth && hasHeight) // Both explicit: used verbatim, even if that distorts the aspect ratio -- that's documented as the caller's choice.
    {
        pixelWidth = args.value(QStringLiteral("width")).toInt();   // Caller's explicit device width.
        pixelHeight = args.value(QStringLiteral("height")).toInt(); // Caller's explicit device height.
    }
    else if (hasWidth) // Only width given: derive height from the bounding box's aspect ratio.
    {
        pixelWidth = args.value(QStringLiteral("width")).toInt(); // Caller's explicit device width.
        // qMax(..., 1.0) guards a degenerate (zero-width) sourceRect -- e.g. content built entirely
        // from a single perfectly vertical line -- from producing a divide-by-zero/NaN aspect ratio.
        const qreal aspect = sourceRect.height() / qMax(sourceRect.width(), 1.0);
        pixelHeight = qMax(1, qRound(pixelWidth * aspect)); // At least 1 unit tall so the device's constructor never receives a zero/negative size.
    }
    else if (hasHeight) // Only height given: derive width from the bounding box's aspect ratio.
    {
        pixelHeight = args.value(QStringLiteral("height")).toInt(); // Caller's explicit device height.
        const qreal aspect = sourceRect.width() / qMax(sourceRect.height(), 1.0); // Same divide-by-zero guard as above, for a degenerate zero-height rect.
        pixelWidth = qMax(1, qRound(pixelHeight * aspect)); // At least 1 unit wide, for the same reason as above.
    }
    else // Neither given: render at a 1:1 scene-unit-to-device-unit mapping, then (raster only) clamp.
    {
        pixelWidth = qMax(1, qRound(sourceRect.width()));   // 1:1 mapping: one scene unit becomes one device unit.
        pixelHeight = qMax(1, qRound(sourceRect.height())); // 1:1 mapping: one scene unit becomes one device unit.

        // Memory-safety cap: only meaningful for a raster (QImage) target -- at Format_ARGB32 (4
        // bytes/pixel), 4096x4096 is ~64MB; an unbounded 1:1 mapping against a huge/runaway draft
        // could otherwise try to allocate a multi-gigabyte QImage and crash the process. A vector/
        // DXF device never allocates a pixel buffer, so export.scene's non-raster formats skip this
        // (applyRasterCap == false) and render at the content's true 1:1 size instead.
        if (applyRasterCap)
        {
            const int largerDimension = qMax(pixelWidth, pixelHeight); // Whichever axis is longer decides whether/how much to scale down.
            const int maxDimension = 4096; // Same cap render.snapshot has always used.
            if (largerDimension > maxDimension) // Only scale down when the cap is actually exceeded.
            {
                const qreal scale = static_cast<qreal>(maxDimension) / static_cast<qreal>(largerDimension); // Uniform factor so the aspect ratio is preserved while scaling down.
                pixelWidth = qMax(1, qRound(pixelWidth * scale));   // Scaled-down width, still at least 1 unit.
                pixelHeight = qMax(1, qRound(pixelHeight * scale)); // Scaled-down height, still at least 1 unit.
            }
        }
    }

    result.ok = true;
    result.sourceRect = sourceRect;
    result.pixelWidth = pixelWidth;
    result.pixelHeight = pixelHeight;
    return result;
}
