//---------------------------------------------------------------------------------------------------------------------
//  @file   render_handlers.cpp
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

#include "render_handlers.h" // Brings in the ActionResult-returning handleRenderSnapshot declaration this file implements.

#include "../action_context.h" // Brings in ActionContext, supplying the scene/doc/data this handler reads.
#include "../name_resolver.h"  // Brings in NameResolver::idForName(), used to resolve "highlight" entries to ids.

#include "../../vwidgets/vmaingraphicsscene.h" // Brings in the full VMainGraphicsScene definition (a QGraphicsScene) so itemsBoundingRect()/render() are callable.
#include "../../ifc/xml/vabstractpattern.h"    // Brings in VAbstractPattern::getTool(id), used to look up a highlighted object's live tool instance.
#include "../../ifc/exception/vexception.h"    // Brings in VExceptionBadId, thrown by getTool() for an id with no registered tool.
#include "../../vmisc/vabstractapplication.h"  // Brings in the qApp macro and VAbstractApplication::Settings(), mirroring mainwindowsnogui.cpp's export functions.
#include "../../vmisc/vcommonsettings.h"       // Brings in VCommonSettings::getLabelFont()/getExportQuality(), used exactly as mainwindowsnogui.cpp's exportPNG() etc. do.

// VDataTool's full definition is required (not just a forward declaration) for the
// dynamic_cast<QGraphicsItem *> below to work: dynamic_cast needs to see the source type is
// polymorphic. This is the one dependency this handler pulls in from vtools; actionlayer.pro
// does not link libvtools itself (a static lib doesn't need to -- see actiond.pro/ActionLayerTest.pro,
// which already both link it for the final executable), so this only requires the header to be
// on the include path, which libs.pri already provides.
#include "../../vtools/tools/vdatatool.h"

#include <QBrush>          // Provides QBrush, used both to clear the default fill and to paint the highlight overlays.
#include <QColor>          // Provides QColor, used to resolve the "background" and highlight-overlay colors.
#include <QDebug>          // Provides qWarning(), used to log skipped highlight names and unmapped formats.
#include <QDir>            // Provides QDir::mkpath(), used to create the output path's parent directory.
#include <QFileInfo>       // Provides QFileInfo, used to derive the output extension and the resolved absolute path.
#include <QGraphicsItem>   // Provides QGraphicsItem, the type highlighted tools are cast to for sceneBoundingRect().
#include <QImage>          // Provides QImage, the in-memory render target this handler saves to disk.
#include <QJsonArray>      // Provides QJsonArray, used for the "highlight"/"highlighted"/"skippedHighlights" arrays.
#include <QJsonObject>     // Provides QJsonObject, the shape of both args and the returned payload.
#include <QPainter>        // Provides QPainter, used to render the scene and draw highlight overlays into the QImage.
#include <QPen>            // Provides Qt::NoPen, used to draw borderless highlight overlay rectangles.
#include <QRectF>          // Provides QRectF, used throughout for the scene-space source rect and per-item bounds.
#include <QVector>         // Provides QVector, used to carry resolved highlight rects from the resolve loop to the draw loop.

namespace
{
    // Maps a lowercase file extension to this handler's canonical, uppercase format name.
    // Mirrors exactly the five formats mainwindowsnogui.cpp's exportPNG/exportTIF/exportJPG/
    // exportBMP/exportPPM support; anything else is "unrecognized" and falls back to PNG below.
    QString formatFromExtension(const QString &extension)
    {
        const QString ext = extension.toLower(); // Normalize case so ".PNG"/".png" behave identically.
        if (ext == QStringLiteral("png")) { return QStringLiteral("PNG"); } // Matches exportPNG's format string.
        if (ext == QStringLiteral("jpg") || ext == QStringLiteral("jpeg")) { return QStringLiteral("JPG"); } // Matches exportJPG's format string.
        if (ext == QStringLiteral("bmp")) { return QStringLiteral("BMP"); } // Matches exportBMP's format string.
        if (ext == QStringLiteral("tif") || ext == QStringLiteral("tiff")) { return QStringLiteral("TIF"); } // Matches exportTIF's format string.
        if (ext == QStringLiteral("ppm")) { return QStringLiteral("PPM"); } // Matches exportPPM's format string.
        return QString(); // Unrecognized extension; caller falls back to the "PNG" default.
    }

    // Returns true if the given (already-uppercased) format name is one of the five this handler
    // supports, i.e. one QImage::save() can be asked to write via the same format string
    // mainwindowsnogui.cpp already uses for each of its export* functions.
    bool isSupportedFormat(const QString &format)
    {
        return format == QStringLiteral("PNG") || format == QStringLiteral("JPG") || format == QStringLiteral("BMP")
            || format == QStringLiteral("TIF") || format == QStringLiteral("PPM"); // The exact five formats mainwindowsnogui.cpp exports.
    }

    // Resolves the "background" param (or its format-dependent default) to a QColor to fill the
    // image with. Returns false (leaving *ok untouched by the caller) if the caller passed an
    // unparsable value, so the handler can report a structured error instead of silently guessing.
    bool resolveBackgroundColor(const QJsonObject &args, const QString &format, QColor &outColor)
    {
        // PNG/TIF/PPM default to transparent and JPG/BMP default to white, matching exactly which
        // of mainwindowsnogui.cpp's export functions call image.fill(Qt::transparent) vs Qt::white.
        const QString defaultBackground = (format == QStringLiteral("JPG") || format == QStringLiteral("BMP"))
            ? QStringLiteral("white") : QStringLiteral("transparent");
        const QString background = args.value(QStringLiteral("background")).toString(defaultBackground); // Caller override, or the format-appropriate default.

        if (background == QStringLiteral("transparent")) // Explicit transparent request.
        {
            outColor = QColor(Qt::transparent); // Fully transparent fill, matching exportPNG/exportTIF/exportPPM's Qt::transparent.
            return true; // Recognized value.
        }
        if (background == QStringLiteral("white")) // Explicit white request.
        {
            outColor = QColor(Qt::white); // Opaque white fill, matching exportJPG/exportBMP's Qt::white.
            return true; // Recognized value.
        }

        const QColor parsed(background); // Attempt to parse anything else (expected to be "#RRGGBB") via QColor's named-color/hex constructor.
        if (!parsed.isValid()) // QColor's constructor leaves an invalid color for anything it can't parse.
        {
            return false; // Neither a keyword nor a valid hex color; caller reports this as a structured error.
        }
        outColor = parsed; // A valid "#RRGGBB" (or any other Qt-recognized color name) was supplied.
        return true; // Recognized value.
    }
}

// Implements "render.snapshot": rasterizes ctx.scene() (the draft scene) to an image file,
// optionally overlaying semi-transparent highlight rectangles over named objects, and returns
// metadata describing what was written. Every failure path returns ActionResult::failure() with
// a specific, actionable message instead of throwing or crashing, matching the convention the
// registry/engine already rely on (see ActionEngine::run()'s own "Unknown action op" handling).
ActionResult handleRenderSnapshot(const QJsonObject &args, const ActionContext &ctx)
{
    // --- target -----------------------------------------------------------------------------
    const QString target = args.value(QStringLiteral("target")).toString(QStringLiteral("draft")); // Which scene to render; "draft" is the only one ActionContext currently exposes.
    if (target != QStringLiteral("draft")) // ActionContext holds a single VMainGraphicsScene*, populated with the draft scene by every current host (see action_host.cpp); there is no piece scene to render yet.
    {
        return ActionResult::failure(QStringLiteral("unsupported target: '%1' (only 'draft' is available; ActionContext exposes no piece scene yet)").arg(target)); // Explicit error instead of silently ignoring the param.
    }

    VMainGraphicsScene *scene = ctx.scene(); // The (only) scene this phase can render.
    if (scene == nullptr) // Guard against a context constructed without a scene (e.g. some test contexts).
    {
        return ActionResult::failure(QStringLiteral("render.snapshot requires a scene, but none is available in this context")); // Clear, actionable reason.
    }

    // --- path ---------------------------------------------------------------------------------
    const QString path = args.value(QStringLiteral("path")).toString(); // Required output file path.
    if (path.isEmpty()) // No path supplied at all.
    {
        return ActionResult::failure(QStringLiteral("render.snapshot requires a non-empty 'path'")); // Clear, actionable reason.
    }

    const QFileInfo pathInfo(path); // Used both to create the parent directory and to report the resolved absolute path.
    if (!QDir().mkpath(pathInfo.absolutePath())) // Create the parent directory if it doesn't already exist; mkpath() also returns true if it already exists.
    {
        return ActionResult::failure(QStringLiteral("could not create output directory: %1").arg(pathInfo.absolutePath())); // Reported instead of letting QImage::save() fail more confusingly below.
    }

    // --- format -------------------------------------------------------------------------------
    QString format = args.value(QStringLiteral("format")).toString(); // Caller-supplied format, if any.
    if (format.isEmpty()) // No explicit format: derive one from the path's extension.
    {
        format = formatFromExtension(pathInfo.suffix()); // Maps ".png"/".jpg"/etc. to the canonical uppercase name.
        if (format.isEmpty()) // Extension unrecognized (or absent): fall back to PNG per the documented default.
        {
            format = QStringLiteral("PNG"); // Matches this op's documented default output format.
        }
    }
    else
    {
        format = format.toUpper(); // Normalize caller input ("png", "Png", ...) before validating/using it.
    }
    if (!isSupportedFormat(format)) // Reject anything outside the five formats this handler (and mainwindowsnogui.cpp) support.
    {
        return ActionResult::failure(QStringLiteral("unsupported format: '%1' (expected one of PNG, JPG, BMP, TIF, PPM)").arg(format)); // Explicit error instead of silently defaulting.
    }

    // --- background ---------------------------------------------------------------------------
    QColor backgroundColor; // Populated by resolveBackgroundColor() below.
    if (!resolveBackgroundColor(args, format, backgroundColor)) // Only fails for an unparsable explicit "background" value.
    {
        return ActionResult::failure(QStringLiteral("unrecognized 'background' value; expected 'transparent', 'white', or '#RRGGBB'")); // Explicit error instead of silently guessing a color.
    }

    // --- padding / bounding box ----------------------------------------------------------------
    const qreal padding = args.value(QStringLiteral("padding")).toDouble(20.0); // Margin added around the content on every side.

    // items().isEmpty() (rather than itemsBoundingRect().isEmpty()) is the correct "truly nothing
    // to render" check: a single perfectly horizontal or vertical line item has a bounding rect
    // with zero width or height -- and QRectF::isEmpty() would then (wrongly) report the whole
    // scene as empty even though it has real content to draw.
    if (scene->items().isEmpty()) // No items at all: nothing meaningful can be rendered.
    {
        return ActionResult::failure(QStringLiteral("empty scene, nothing to render")); // Structured error rather than producing a degenerate (0x0 or blank) image.
    }

    const QRectF itemsRect = scene->itemsBoundingRect(); // Tight bounds of everything currently drawn, in scene coordinates.
    // itemsBoundingRect() (not sceneRect()) is used deliberately: sceneRect() reflects the
    // editor's configured/scrollable canvas size, which is usually much larger than the drawn
    // content and would produce a mostly-blank image; itemsBoundingRect() crops to what's actually there.
    const QRectF sourceRect = itemsRect.adjusted(-padding, -padding, padding, padding); // Expand by padding on all four sides.

    // --- pixel size -----------------------------------------------------------------------------
    const bool hasWidth = args.contains(QStringLiteral("width"));   // Caller supplied an explicit width.
    const bool hasHeight = args.contains(QStringLiteral("height")); // Caller supplied an explicit height.

    int pixelWidth = 0;  // Populated by exactly one of the three branches below.
    int pixelHeight = 0; // Populated by exactly one of the three branches below.

    if (hasWidth && hasHeight) // Both explicit: used verbatim, even if that distorts the aspect ratio -- that's documented as the caller's choice.
    {
        pixelWidth = args.value(QStringLiteral("width")).toInt();   // Caller's explicit pixel width.
        pixelHeight = args.value(QStringLiteral("height")).toInt(); // Caller's explicit pixel height.
    }
    else if (hasWidth) // Only width given: derive height from the bounding box's aspect ratio.
    {
        pixelWidth = args.value(QStringLiteral("width")).toInt(); // Caller's explicit pixel width.
        // qMax(..., 1.0) guards a degenerate (zero-width) sourceRect -- e.g. content built entirely
        // from a single perfectly vertical line -- from producing a divide-by-zero/NaN aspect ratio.
        const qreal aspect = sourceRect.height() / qMax(sourceRect.width(), 1.0);
        pixelHeight = qMax(1, qRound(pixelWidth * aspect)); // At least 1px tall so QImage's constructor never receives a zero/negative size.
    }
    else if (hasHeight) // Only height given: derive width from the bounding box's aspect ratio.
    {
        pixelHeight = args.value(QStringLiteral("height")).toInt(); // Caller's explicit pixel height.
        const qreal aspect = sourceRect.width() / qMax(sourceRect.height(), 1.0); // Same divide-by-zero guard as above, for a degenerate zero-height rect.
        pixelWidth = qMax(1, qRound(pixelHeight * aspect)); // At least 1px wide, for the same reason as above.
    }
    else // Neither given: render at a 1:1 scene-unit-to-pixel mapping, then clamp.
    {
        pixelWidth = qMax(1, qRound(sourceRect.width()));   // 1:1 mapping: one scene unit becomes one pixel.
        pixelHeight = qMax(1, qRound(sourceRect.height())); // 1:1 mapping: one scene unit becomes one pixel.

        const int largerDimension = qMax(pixelWidth, pixelHeight); // Whichever axis is longer decides whether/how much to scale down.
        const int maxDimension = 4096; // Memory-safety cap: at Format_ARGB32 (4 bytes/pixel), 4096x4096 is ~64MB; an unbounded 1:1 mapping against a huge/runaway draft could otherwise try to allocate a multi-gigabyte QImage and crash the process.
        if (largerDimension > maxDimension) // Only scale down when the cap is actually exceeded.
        {
            const qreal scale = static_cast<qreal>(maxDimension) / static_cast<qreal>(largerDimension); // Uniform factor so the aspect ratio is preserved while scaling down.
            pixelWidth = qMax(1, qRound(pixelWidth * scale));   // Scaled-down width, still at least 1px.
            pixelHeight = qMax(1, qRound(pixelHeight * scale)); // Scaled-down height, still at least 1px.
        }
    }

    // --- resolve highlight names to scene-space rects (before rendering, since NameResolver/getTool don't depend on the render itself) ---
    QJsonArray highlightNamesArg = args.value(QStringLiteral("highlight")).toArray(); // Optional list of object names to mark; empty if absent.
    QJsonArray highlighted;       // Names that were successfully resolved and will be overlaid.
    QJsonArray skippedHighlights; // Names that could not be resolved to a live graphics item.
    QVector<QRectF> highlightRects; // Scene-space bounding rects, in the same order as `highlighted`, drawn after the main render below.

    for (const QJsonValue &nameValue : highlightNamesArg) // Walk every requested highlight name, in caller-supplied order.
    {
        const QString name = nameValue.toString(); // The object name this entry asks to highlight.
        if (name.isEmpty()) // A blank entry carries nothing to resolve or report; skip it without adding noise to either output list.
        {
            continue; // Nothing meaningful to do with an empty name.
        }

        const quint32 id = NameResolver::idForName(name, ctx.data()); // Resolve the name to an internal id; 0 signals "not resolved" (see name_resolver.cpp).
        if (id == 0) // Unresolvable name (includes every name until NameResolver's Phase 2 implementation lands).
        {
            qWarning() << "render.snapshot: could not resolve highlight name to an id:" << name; // Logged per the spec, but does not fail the whole action.
            skippedHighlights.append(name); // Reported back to the caller as skipped.
            continue; // Move on to the next requested name.
        }

        // Phase 8: VAbstractPattern::getTool() throws VExceptionBadId (not nullptr) for an id with
        // no registered tool -- a state that is now reachable for a real, resolved object: e.g. a
        // VToolMove/VToolRotation/VToolMirrorByLine/VToolMirrorByAxis (operation_handlers.cpp)
        // destination point is a plain VPointF added via VContainer::AddGObject() with no
        // individual tool of its own (only the *operation* tool, at a different id, is
        // registered). The `if (tool == nullptr)` guard below was already dead code even before
        // Phase 8 (getTool() never returns nullptr), so this try/catch is the actual fix, not the
        // guard beneath it (kept for clarity/documentation, though unreachable).
        VDataTool *tool = nullptr;
        try
        {
            tool = VAbstractPattern::getTool(id); // Look up the live tool instance for this id, if any.
        }
        catch (const VExceptionBadId &)
        {
            tool = nullptr; // No tool registered for this id; handled uniformly by the guard below.
        }
        if (tool == nullptr) // The id doesn't correspond to a currently-registered tool.
        {
            qWarning() << "render.snapshot: no live tool found for highlight id" << id << "(name" << name << ")"; // Logged, not fatal.
            skippedHighlights.append(name); // Reported back to the caller as skipped.
            continue; // Move on to the next requested name.
        }

        // Concrete tool classes (VToolBasePoint, VToolLine, ...) multiply-inherit both VDataTool
        // and a QGraphicsItem-derived visualization class; this cross-cast is the same idiom
        // mainwindow.cpp uses elsewhere (dynamic_cast<QGraphicsItem *>(...) on a freshly-created tool).
        QGraphicsItem *item = dynamic_cast<QGraphicsItem *>(tool);
        if (item == nullptr) // This tool type has no associated graphics item (or none was created yet).
        {
            qWarning() << "render.snapshot: highlight target has no graphics item:" << name; // Logged, not fatal.
            skippedHighlights.append(name); // Reported back to the caller as skipped.
            continue; // Move on to the next requested name.
        }

        highlighted.append(name); // This name resolved to a real, drawable graphics item.
        highlightRects.append(item->sceneBoundingRect()); // Its scene-space bounds, transformed to image pixels after the main render below.
    }

    // --- render -------------------------------------------------------------------------------
    QImage image(QSize(pixelWidth, pixelHeight), QImage::Format_ARGB32); // Format_ARGB32 (not RGB32) is required so a "transparent" background actually has a usable alpha channel.
    image.fill(backgroundColor); // Uniform fill: fully transparent, opaque white, or the caller's custom color.

    QPainter painter(&image); // Paints directly into image's pixel buffer (QImage is a raster paint device, so no explicit flush/end() is needed before image.save() below).
    painter.setFont(qApp->Settings()->getLabelFont()); // Matches mainwindowsnogui.cpp's export* functions; qApp->Settings() (not the app-specific Seamly2DSettings()) is used because actionlayer is a standalone static library that doesn't link against seamly2d's Application2D subclass.
    painter.setRenderHint(QPainter::Antialiasing, true); // Matches mainwindowsnogui.cpp's export* functions: smooths curves/lines instead of producing jagged output.
    painter.setBrush(QBrush(Qt::NoBrush)); // Matches mainwindowsnogui.cpp's export* functions: avoids an unwanted default fill on shapes the scene renders.

    // The target/source-rect overload (as used at the layout IgnoreAspectRatio call sites in
    // mainwindowsnogui.cpp) is required here, unlike the plain exportPNG()'s no-argument render():
    // it is what performs the crop-to-content (sourceRect) and scale-to-pixelSize in one call.
    scene->render(&painter, QRectF(0, 0, pixelWidth, pixelHeight), sourceRect, Qt::IgnoreAspectRatio);

    // --- highlight overlays (drawn after the main render, in the same still-open painter) ------
    if (!highlightRects.isEmpty()) // Only compute the transform if there's actually something to overlay.
    {
        // Same scene-space-to-pixel-space transform QGraphicsScene::render() itself just applied
        // above (sourceRect -> the 0,0,pixelWidth,pixelHeight target rect), reproduced manually so
        // each highlighted item's bounds land in the correct spot in image pixel coordinates.
        const qreal scaleX = pixelWidth / sourceRect.width();   // Horizontal scene-unit-to-pixel factor.
        const qreal scaleY = pixelHeight / sourceRect.height(); // Vertical scene-unit-to-pixel factor.

        painter.setPen(Qt::NoPen); // No border: a flat translucent fill is enough to mark the item without obscuring its edges further.
        const QColor overlayColor(255, 64, 64, 90); // Semi-transparent red: visible over both light and dark pattern content without fully hiding it.
        painter.setBrush(overlayColor); // Used by fillRect-equivalent drawRect below.

        for (const QRectF &sceneItemRect : highlightRects) // One overlay rectangle per successfully-resolved highlight.
        {
            const QRectF pixelRect( // Map the item's scene-space rect into image-pixel space using the same transform as the main render.
                (sceneItemRect.x() - sourceRect.x()) * scaleX,
                (sceneItemRect.y() - sourceRect.y()) * scaleY,
                sceneItemRect.width() * scaleX,
                sceneItemRect.height() * scaleY);
            painter.drawRect(pixelRect); // Draws the translucent overlay rectangle for this highlighted item.
        }
    }

    // --- save ---------------------------------------------------------------------------------
    const bool saved = image.save(path, format.toUtf8().constData(), qApp->Settings()->getExportQuality()); // Mirrors mainwindowsnogui.cpp's export* functions' image.save() call exactly.
    if (!saved) // QImage::save() returning false covers e.g. an unwritable path or a plugin-level encode failure.
    {
        return ActionResult::failure(QStringLiteral("failed to save image to: %1").arg(path)); // Specific, actionable reason naming the path that failed.
    }

    // --- success payload ------------------------------------------------------------------------
    QJsonObject pixelSize; // "pixelSize": the actual dimensions of the saved image.
    pixelSize["width"] = pixelWidth;   // Final rendered width, after any derivation/clamping above.
    pixelSize["height"] = pixelHeight; // Final rendered height, after any derivation/clamping above.

    QJsonObject boundingBox; // "boundingBox": the (padded) scene-space rect that was actually rendered.
    boundingBox["x"] = sourceRect.x();
    boundingBox["y"] = sourceRect.y();
    boundingBox["width"] = sourceRect.width();
    boundingBox["height"] = sourceRect.height();

    QJsonObject payload; // Assembles the documented "render.snapshot" success shape.
    payload["op"] = QStringLiteral("render.snapshot");   // Redundant with the engine's own envelope "op" field, but explicitly requested in this op's documented payload shape.
    payload["status"] = QStringLiteral("ok");             // Ditto: mirrors the engine's "ok" field for a caller reading only the payload.
    payload["path"] = pathInfo.absoluteFilePath();         // Resolved absolute path actually written to.
    payload["pixelSize"] = pixelSize;                      // Actual saved image dimensions.
    payload["boundingBox"] = boundingBox;                  // The padded scene-space rect that was rendered.
    payload["highlighted"] = highlighted;                  // Names successfully resolved and overlaid.
    payload["skippedHighlights"] = skippedHighlights;      // Names that could not be resolved or had no graphics item.

    return ActionResult::success(payload); // Wrap the payload as a successful result for the registry/engine to return.
}
