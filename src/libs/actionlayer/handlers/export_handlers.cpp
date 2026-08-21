//---------------------------------------------------------------------------------------------------------------------
//  @file   export_handlers.cpp
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

#include "export_handlers.h" // Brings in the ActionResult-returning handleExportScene declaration this file implements.

#include "../action_context.h"    // Brings in ActionContext, supplying the scene this handler reads.
#include "scene_render_geometry.h" // Brings in computeSceneRenderGeometry(), shared with render_handlers.cpp.

#include "../../vwidgets/vmaingraphicsscene.h" // Brings in the full VMainGraphicsScene definition (a QGraphicsScene) so items()/render() are callable.
#include "../../vmisc/vabstractapplication.h"  // Brings in the qApp macro and VAbstractApplication::Settings(), mirroring render_handlers.cpp/mainwindowsnogui.cpp's export functions.
#include "../../vmisc/vcommonsettings.h"       // Brings in VCommonSettings::getLabelFont()/getExportQuality(), used exactly as render_handlers.cpp does.
#include "../../vmisc/def.h"                   // Brings in PrintDPI/FromPixel()/Unit, used to size the PDF/PS/EPS page in real-world (mm) units.
#include "../../ifc/exception/vexception.h"    // Brings in VException, caught around every writer below per the task's DoExport()-style convention.
#include "../../vdxf/vdxfpaintdevice.h"        // Brings in VDxfPaintDevice (a plain QPaintDevice, no GUI/MainWindow dependency) and, transitively, DRW::Version.

#include <QBrush>          // Provides QBrush, used to clear the default fill before rendering, matching render_handlers.cpp.
#include <QColor>          // Provides QColor, used to resolve the raster "background" param.
#include <QDir>            // Provides QDir::mkpath(), used to create the output path's parent directory.
#include <QFileInfo>       // Provides QFileInfo, used to derive the output extension and the resolved absolute path.
#include <QHash>           // Provides QHash, used by the format-name/extension lookup tables below.
#include <QImage>          // Provides QImage, the raster (PNG/JPG/BMP/PPM/TIF) render target.
#include <QJsonObject>     // Provides QJsonObject, the shape of both args and the returned payload.
#include <QPageLayout>     // Provides QPageLayout::Portrait, the PDF page orientation.
#include <QPageSize>       // Provides QPageSize, used to size the PDF page to the exported content.
#include <QPainter>        // Provides QPainter, used to render the scene into every device below.
#include <QPen>            // Provides QPen, used to match mainwindowsnogui.cpp's exportPDF pen setup.
#include <QPrinter>        // Provides QPrinter, the PDF (and, via a temp PDF, PS/EPS) render target.
#include <QProcess>        // Provides QProcess, used to shell out to the external "pdftops" tool for PS/EPS, exactly as MainWindowsNoGUI::convertPdfToPs() does.
#include <QRect>           // Provides QRect, used for QSvgGenerator::setViewBox()'s integer-rect overload.
#include <QRectF>          // Provides QRectF, used throughout for the scene-space source rect and the device-space target rect.
#include <QScopedPointer>  // Provides QScopedPointer, giving the optional "showPointNames" override RAII cleanup without a manual delete.
#include <QSize>           // Provides QSize, the device-size type every non-vector-source QPaintDevice setter below takes.
#include <QSvgGenerator>   // Provides QSvgGenerator, the SVG render target.
#include <QTemporaryFile>  // Provides QTemporaryFile, the intermediate PDF file PS/EPS export renders into before invoking pdftops.

namespace
{
    // Every format export.scene supports, plus the DXF version each dxf-* string resolves to.
    // Deliberately a closed set of caller-facing strings (see formatFromString()/formatFromExtension()
    // below) rather than the raw LayoutExportFormat ordinals DoExport()'s own "--format" CLI flag
    // uses -- a JSON caller should never need to know those enum numbers.
    enum class SceneExportFormat
    {
        Svg, Pdf, Ps, Eps,
        Png, Jpg, Bmp, Ppm, Tif,
        DxfR10, DxfR12, DxfR13, DxfR14, Dxf2000, Dxf2004, Dxf2007, Dxf2010, Dxf2013
    };

    bool isRasterFormat(SceneExportFormat format)
    {
        return format == SceneExportFormat::Png || format == SceneExportFormat::Jpg || format == SceneExportFormat::Bmp
            || format == SceneExportFormat::Ppm || format == SceneExportFormat::Tif;
    }

    // Returns the format string this op's schema documents, i.e. the string a caller could have
    // passed as "format" to get this same value back -- used only to echo a canonical value in the
    // success payload's "format" field (useful when the caller relied on extension-based inference).
    QString formatToCanonicalString(SceneExportFormat format)
    {
        switch (format)
        {
            case SceneExportFormat::Svg: return QStringLiteral("svg");
            case SceneExportFormat::Pdf: return QStringLiteral("pdf");
            case SceneExportFormat::Ps: return QStringLiteral("ps");
            case SceneExportFormat::Eps: return QStringLiteral("eps");
            case SceneExportFormat::Png: return QStringLiteral("png");
            case SceneExportFormat::Jpg: return QStringLiteral("jpg");
            case SceneExportFormat::Bmp: return QStringLiteral("bmp");
            case SceneExportFormat::Ppm: return QStringLiteral("ppm");
            case SceneExportFormat::Tif: return QStringLiteral("tif");
            case SceneExportFormat::DxfR10: return QStringLiteral("dxf-r10");
            case SceneExportFormat::DxfR12: return QStringLiteral("dxf-r12");
            case SceneExportFormat::DxfR13: return QStringLiteral("dxf-r13");
            case SceneExportFormat::DxfR14: return QStringLiteral("dxf-r14");
            case SceneExportFormat::Dxf2000: return QStringLiteral("dxf-2000");
            case SceneExportFormat::Dxf2004: return QStringLiteral("dxf-2004");
            case SceneExportFormat::Dxf2007: return QStringLiteral("dxf-2007");
            case SceneExportFormat::Dxf2010: return QStringLiteral("dxf-2010");
            case SceneExportFormat::Dxf2013: return QStringLiteral("dxf-2013");
        }
        return QString(); // Unreachable (every enumerator handled above); silences -Wreturn-type on some compilers.
    }

    // QImage::save()'s own format-name argument for the five raster formats; distinct from
    // formatToCanonicalString() above, which returns this op's lowercase schema string instead.
    QByteArray rasterQtFormatName(SceneExportFormat format)
    {
        switch (format)
        {
            case SceneExportFormat::Png: return QByteArrayLiteral("PNG");
            case SceneExportFormat::Jpg: return QByteArrayLiteral("JPG");
            case SceneExportFormat::Bmp: return QByteArrayLiteral("BMP");
            case SceneExportFormat::Ppm: return QByteArrayLiteral("PPM");
            case SceneExportFormat::Tif: return QByteArrayLiteral("TIF");
            default: return QByteArray(); // Never called for a non-raster format.
        }
    }

    // Maps a dxf-* SceneExportFormat to the AutoCAD version VDxfPaintDevice::setVersion() expects.
    // Only meaningful when the format is one of the nine DxfXxx enumerators.
    DRW::Version dxfVersion(SceneExportFormat format)
    {
        switch (format)
        {
            case SceneExportFormat::DxfR10: return DRW::AC1006;
            case SceneExportFormat::DxfR12: return DRW::AC1009;
            case SceneExportFormat::DxfR13: return DRW::AC1012;
            case SceneExportFormat::DxfR14: return DRW::AC1014;
            case SceneExportFormat::Dxf2000: return DRW::AC1015;
            case SceneExportFormat::Dxf2004: return DRW::AC1018;
            case SceneExportFormat::Dxf2007: return DRW::AC1021;
            case SceneExportFormat::Dxf2010: return DRW::AC1024;
            case SceneExportFormat::Dxf2013: return DRW::AC1027;
            default: return DRW::UNKNOWNV; // Never called for a non-DXF format.
        }
    }

    // Parses an explicit "format" argument (case-insensitive) into a SceneExportFormat. Returns
    // false for anything not in the table this op's schema documents -- callers get a specific
    // "unsupported format" error listing the real set, not a silent fallback.
    bool formatFromString(const QString &name, SceneExportFormat &format)
    {
        const QString lower = name.toLower();
        static const QHash<QString, SceneExportFormat> table{
            {QStringLiteral("svg"), SceneExportFormat::Svg},
            {QStringLiteral("pdf"), SceneExportFormat::Pdf},
            {QStringLiteral("ps"), SceneExportFormat::Ps},
            {QStringLiteral("eps"), SceneExportFormat::Eps},
            {QStringLiteral("png"), SceneExportFormat::Png},
            {QStringLiteral("jpg"), SceneExportFormat::Jpg},
            {QStringLiteral("bmp"), SceneExportFormat::Bmp},
            {QStringLiteral("ppm"), SceneExportFormat::Ppm},
            {QStringLiteral("tif"), SceneExportFormat::Tif},
            {QStringLiteral("dxf-r10"), SceneExportFormat::DxfR10},
            {QStringLiteral("dxf-r12"), SceneExportFormat::DxfR12},
            {QStringLiteral("dxf-r13"), SceneExportFormat::DxfR13},
            {QStringLiteral("dxf-r14"), SceneExportFormat::DxfR14},
            {QStringLiteral("dxf-2000"), SceneExportFormat::Dxf2000},
            {QStringLiteral("dxf-2004"), SceneExportFormat::Dxf2004},
            {QStringLiteral("dxf-2007"), SceneExportFormat::Dxf2007},
            {QStringLiteral("dxf-2010"), SceneExportFormat::Dxf2010},
            {QStringLiteral("dxf-2013"), SceneExportFormat::Dxf2013},
        };
        const auto it = table.constFind(lower);
        if (it == table.constEnd())
        {
            return false;
        }
        format = it.value();
        return true;
    }

    // Infers a format from a (lowercased, no leading dot) file extension, for every extension that
    // maps unambiguously to exactly one format. ".dxf" is deliberately not handled here -- it spans
    // nine formats, so the caller (handleExportScene) requires an explicit "format" for it instead
    // of guessing one.
    bool formatFromExtension(const QString &extension, SceneExportFormat &format)
    {
        static const QHash<QString, SceneExportFormat> table{
            {QStringLiteral("svg"), SceneExportFormat::Svg},
            {QStringLiteral("pdf"), SceneExportFormat::Pdf},
            {QStringLiteral("ps"), SceneExportFormat::Ps},
            {QStringLiteral("eps"), SceneExportFormat::Eps},
            {QStringLiteral("png"), SceneExportFormat::Png},
            {QStringLiteral("jpg"), SceneExportFormat::Jpg},
            {QStringLiteral("jpeg"), SceneExportFormat::Jpg},
            {QStringLiteral("bmp"), SceneExportFormat::Bmp},
            {QStringLiteral("ppm"), SceneExportFormat::Ppm},
            {QStringLiteral("tif"), SceneExportFormat::Tif},
            {QStringLiteral("tiff"), SceneExportFormat::Tif},
        };
        const auto it = table.constFind(extension.toLower());
        if (it == table.constEnd())
        {
            return false;
        }
        format = it.value();
        return true;
    }

    // Resolves the raster-only "background" param (or its format-dependent default) to a QColor.
    // Copy of render_handlers.cpp's own resolveBackgroundColor(): kept local rather than shared,
    // since (unlike the padding/sizing math in scene_render_geometry.cpp) this is a small,
    // raster-format-specific concern export.scene's other formats never call at all.
    bool resolveBackgroundColor(const QJsonObject &args, const QString &rasterFormat, QColor &outColor)
    {
        const QString defaultBackground = (rasterFormat == QStringLiteral("JPG") || rasterFormat == QStringLiteral("BMP"))
            ? QStringLiteral("white") : QStringLiteral("transparent");
        const QString background = args.value(QStringLiteral("background")).toString(defaultBackground);

        if (background == QStringLiteral("transparent"))
        {
            outColor = QColor(Qt::transparent);
            return true;
        }
        if (background == QStringLiteral("white"))
        {
            outColor = QColor(Qt::white);
            return true;
        }
        const QColor parsed(background);
        if (!parsed.isValid())
        {
            return false;
        }
        outColor = parsed;
        return true;
    }

    // Temporarily overrides qApp->Settings()'s scene-wide point-name-label visibility flag for the
    // duration of one export.scene call. Copy of render_handlers.cpp's own ScopedPointNameVisibility
    // -- see that file's class comment for why the boolean sense here is deliberately non-inverted
    // relative to VCommonSettings::getHidePointNames()'s own (confusingly-named) semantics.
    class ScopedPointNameVisibility
    {
    public:
        explicit ScopedPointNameVisibility(bool showPointNames)
            : m_previous(qApp->Settings()->getHidePointNames())
        {
            qApp->Settings()->setHidePointNames(showPointNames);
        }
        ~ScopedPointNameVisibility()
        {
            qApp->Settings()->setHidePointNames(m_previous);
        }
    private:
        bool m_previous;
    };

    // Shared QPainter setup every vector/DXF writer below applies before scene->render(): matches
    // render_handlers.cpp's raster painter setup (antialiasing on, no default brush fill) so every
    // format renders visually consistent line/curve output.
    void preparePainter(QPainter &painter)
    {
        painter.setFont(qApp->Settings()->getLabelFont());
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setBrush(QBrush(Qt::NoBrush));
    }

    bool writeRaster(VMainGraphicsScene *scene, const QString &path, SceneExportFormat format,
                      const QColor &backgroundColor, int pixelWidth, int pixelHeight, const QRectF &sourceRect,
                      QString &errorMessage)
    {
        QImage image(QSize(pixelWidth, pixelHeight), QImage::Format_ARGB32); // ARGB32 (not RGB32) so a "transparent" background has a usable alpha channel.
        image.fill(backgroundColor);

        QPainter painter(&image);
        preparePainter(painter);
        scene->render(&painter, QRectF(0, 0, pixelWidth, pixelHeight), sourceRect, Qt::IgnoreAspectRatio);
        painter.end();

        if (!image.save(path, rasterQtFormatName(format).constData(), qApp->Settings()->getExportQuality()))
        {
            errorMessage = QStringLiteral("failed to save image to: %1").arg(path);
            return false;
        }
        return true;
    }

    bool writeSvg(VMainGraphicsScene *scene, const QString &path, int pixelWidth, int pixelHeight,
                  const QRectF &sourceRect, QString &errorMessage)
    {
        QSvgGenerator generator;
        generator.setFileName(path);
        generator.setSize(QSize(pixelWidth, pixelHeight));
        generator.setViewBox(QRect(0, 0, pixelWidth, pixelHeight));
        generator.setResolution(qRound(PrintDPI));

        QPainter painter;
        if (!painter.begin(&generator))
        {
            errorMessage = QStringLiteral("failed to open SVG output: %1").arg(path);
            return false;
        }
        preparePainter(painter);
        scene->render(&painter, QRectF(0, 0, pixelWidth, pixelHeight), sourceRect, Qt::IgnoreAspectRatio);
        painter.end();

        if (!QFileInfo::exists(path))
        {
            errorMessage = QStringLiteral("failed to write SVG output: %1").arg(path);
            return false;
        }
        return true;
    }

    bool writePdf(VMainGraphicsScene *scene, const QString &path, int pixelWidth, int pixelHeight,
                  const QRectF &sourceRect, QString &errorMessage)
    {
        QPrinter printer;
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setOutputFileName(path);
        printer.setResolution(qRound(PrintDPI));
        printer.setPageOrientation(QPageLayout::Portrait);
        printer.setFullPage(true); // export.scene has no separate "print margins" concept -- the requested "padding" is already baked into sourceRect.

        const QSizeF pageSizeMm(FromPixel(pixelWidth, Unit::Mm), FromPixel(pixelHeight, Unit::Mm));
        printer.setPageSize(QPageSize(pageSizeMm, QPageSize::Millimeter));

        QPainter painter;
        if (!painter.begin(&printer))
        {
            errorMessage = QStringLiteral("failed to open PDF output: %1").arg(path);
            return false;
        }
        preparePainter(painter);
        painter.setPen(QPen(Qt::black, 1.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        scene->render(&painter, QRectF(0, 0, pixelWidth, pixelHeight), sourceRect, Qt::IgnoreAspectRatio);
        painter.end();

        if (!QFileInfo::exists(path))
        {
            errorMessage = QStringLiteral("failed to write PDF output: %1").arg(path);
            return false;
        }
        return true;
    }

#if defined(Q_OS_WIN)
    const char PDFTOPS_BIN[] = "pdftops.exe";
#else
    const char PDFTOPS_BIN[] = "pdftops";
#endif

    // PS/EPS export has no native Qt writer: mirrors MainWindowsNoGUI::exportPS()/exportEPS()
    // exactly -- render a temporary PDF via writePdf() above, then shell out to the external
    // "pdftops" tool (from Poppler/Xpdf) to convert it. Unlike MainWindowsNoGUI's own
    // convertPdfToPs(), this never pops a QMessageBox on failure -- it reports a structured
    // ActionResult::failure() instead, matching every other headless-safe handler in this module.
    bool writePsOrEps(VMainGraphicsScene *scene, const QString &path, int pixelWidth, int pixelHeight,
                       const QRectF &sourceRect, bool eps, QString &errorMessage)
    {
        QTemporaryFile tempPdf(QDir::tempPath() + QStringLiteral("/actiond-export-XXXXXX.pdf"));
        if (!tempPdf.open())
        {
            errorMessage = QStringLiteral("could not create a temporary PDF for %1 export").arg(eps ? QStringLiteral("EPS") : QStringLiteral("PS"));
            return false;
        }

        QString pdfError;
        if (!writePdf(scene, tempPdf.fileName(), pixelWidth, pixelHeight, sourceRect, pdfError))
        {
            errorMessage = pdfError;
            return false;
        }

        QStringList params;
        if (eps)
        {
            params << QStringLiteral("-eps");
        }
        params << tempPdf.fileName() << path;

        QProcess proc;
        proc.start(QString::fromLatin1(PDFTOPS_BIN), params);
        if (!proc.waitForStarted(15000))
        {
            errorMessage = QStringLiteral("could not start 'pdftops' (required for %1 export; install Poppler/Xpdf and ensure it is on PATH)")
                .arg(eps ? QStringLiteral("EPS") : QStringLiteral("PS"));
            return false;
        }
        proc.waitForFinished(15000);

        if (!QFileInfo::exists(path))
        {
            errorMessage = QStringLiteral("'pdftops' failed to produce output: %1").arg(QString::fromUtf8(proc.readAllStandardError()));
            return false;
        }
        return true;
    }

    bool writeFlatDxf(VMainGraphicsScene *scene, const QString &path, int pixelWidth, int pixelHeight,
                       const QRectF &sourceRect, DRW::Version version, bool binary, QString &errorMessage)
    {
        VDxfPaintDevice generator;
        generator.setFileName(path);
        generator.setSize(QSize(pixelWidth, pixelHeight));
        generator.setResolution(PrintDPI);
        generator.setVersion(version);
        generator.SetBinaryFormat(binary);
        generator.setInsunits(VarInsunits::Millimeters); // Matches mainwindowsnogui.cpp's FlatDxfFile(): always mm, see Seamly2D issue #745.

        QPainter painter;
        if (!painter.begin(&generator))
        {
            errorMessage = QStringLiteral("failed to open DXF output: %1").arg(path);
            return false;
        }
        scene->render(&painter, QRectF(0, 0, pixelWidth, pixelHeight), sourceRect, Qt::IgnoreAspectRatio);
        painter.end();

        if (!QFileInfo::exists(path))
        {
            errorMessage = QStringLiteral("failed to write DXF output: %1").arg(path);
            return false;
        }
        return true;
    }
}

// Implements "export.scene": writes ctx.scene() (the draft scene) to one of eighteen vector/DXF/
// raster formats -- see export_handlers.h and docs/action-layer-schema.md for the full parameter
// reference. Every failure path returns ActionResult::failure() with a specific, actionable
// message; no VException/std::exception is allowed to propagate out of this handler.
ActionResult handleExportScene(const QJsonObject &args, const ActionContext &ctx)
{
    // --- target -----------------------------------------------------------------------------
    const QString target = args.value(QStringLiteral("target")).toString(QStringLiteral("draft")); // Which scene to export; "draft" is the only one ActionContext currently exposes.
    if (target != QStringLiteral("draft"))
    {
        return ActionResult::failure(QStringLiteral("unsupported target: '%1' (only 'draft' is available; ActionContext exposes no piece scene yet)").arg(target));
    }

    VMainGraphicsScene *scene = ctx.scene();
    if (scene == nullptr)
    {
        return ActionResult::failure(QStringLiteral("export.scene requires a scene, but none is available in this context"));
    }

    // --- path ---------------------------------------------------------------------------------
    const QString path = args.value(QStringLiteral("path")).toString();
    if (path.isEmpty())
    {
        return ActionResult::failure(QStringLiteral("export.scene requires a non-empty 'path'"));
    }

    const QFileInfo pathInfo(path);
    if (!QDir().mkpath(pathInfo.absolutePath()))
    {
        return ActionResult::failure(QStringLiteral("could not create output directory: %1").arg(pathInfo.absolutePath()));
    }

    // --- format -------------------------------------------------------------------------------
    SceneExportFormat format = SceneExportFormat::Png; // Overwritten by exactly one branch below before use.
    const QString formatArg = args.value(QStringLiteral("format")).toString();
    if (!formatArg.isEmpty())
    {
        if (!formatFromString(formatArg, format))
        {
            return ActionResult::failure(QStringLiteral(
                "unsupported format: '%1' (expected one of svg, pdf, ps, eps, png, jpg, bmp, ppm, tif, "
                "dxf-r10, dxf-r12, dxf-r13, dxf-r14, dxf-2000, dxf-2004, dxf-2007, dxf-2010, dxf-2013)").arg(formatArg));
        }
    }
    else
    {
        const QString extension = pathInfo.suffix().toLower();
        if (extension == QStringLiteral("dxf"))
        {
            return ActionResult::failure(QStringLiteral(
                "'format' is required when 'path' ends in .dxf -- it spans nine AutoCAD versions "
                "(dxf-r10, dxf-r12, dxf-r13, dxf-r14, dxf-2000, dxf-2004, dxf-2007, dxf-2010, dxf-2013) "
                "that a bare .dxf extension cannot disambiguate"));
        }
        if (!formatFromExtension(extension, format))
        {
            format = SceneExportFormat::Png; // Unrecognized/absent extension: same PNG fallback render.snapshot documents.
        }
    }

    // --- background (raster-only; silently unused for every other format) ---------------------
    QColor backgroundColor;
    if (isRasterFormat(format))
    {
        if (!resolveBackgroundColor(args, rasterQtFormatName(format), backgroundColor))
        {
            return ActionResult::failure(QStringLiteral("unrecognized 'background' value; expected 'transparent', 'white', or '#RRGGBB'"));
        }
    }

    // --- padding / bounding box / device size --------------------------------------------------
    const SceneRenderGeometry geometry = computeSceneRenderGeometry(scene, args, /*applyRasterCap=*/isRasterFormat(format));
    if (!geometry.ok)
    {
        return ActionResult::failure(QStringLiteral("empty scene, nothing to export"));
    }

    // --- point-name label visibility ------------------------------------------------------------
    QScopedPointer<ScopedPointNameVisibility> pointNameVisibility;
    if (args.contains(QStringLiteral("showPointNames")))
    {
        const bool showPointNames = args.value(QStringLiteral("showPointNames")).toBool(true);
        pointNameVisibility.reset(new ScopedPointNameVisibility(showPointNames));
    }

    // --- write --------------------------------------------------------------------------------
    QString writeError;
    bool wrote = false;
    try
    {
        if (isRasterFormat(format))
        {
            wrote = writeRaster(scene, path, format, backgroundColor, geometry.pixelWidth, geometry.pixelHeight, geometry.sourceRect, writeError);
        }
        else if (format == SceneExportFormat::Svg)
        {
            wrote = writeSvg(scene, path, geometry.pixelWidth, geometry.pixelHeight, geometry.sourceRect, writeError);
        }
        else if (format == SceneExportFormat::Pdf)
        {
            wrote = writePdf(scene, path, geometry.pixelWidth, geometry.pixelHeight, geometry.sourceRect, writeError);
        }
        else if (format == SceneExportFormat::Ps || format == SceneExportFormat::Eps)
        {
            wrote = writePsOrEps(scene, path, geometry.pixelWidth, geometry.pixelHeight, geometry.sourceRect,
                                  format == SceneExportFormat::Eps, writeError);
        }
        else // One of the nine dxf-* formats.
        {
            const bool binaryDxf = args.value(QStringLiteral("binaryDXF")).toBool(false);
            wrote = writeFlatDxf(scene, path, geometry.pixelWidth, geometry.pixelHeight, geometry.sourceRect,
                                  dxfVersion(format), binaryDxf, writeError);
        }
    }
    catch (const VException &exception) // No reused Seamly2D writer is expected to throw here, but every new handler wraps its work this way per this module's own convention (see MainWindow::DoExport()).
    {
        return ActionResult::failure(QStringLiteral("export.scene failed: %1").arg(exception.ErrorMessage()));
    }

    if (!wrote)
    {
        return ActionResult::failure(writeError);
    }

    // --- success payload ------------------------------------------------------------------------
    QJsonObject deviceSize;
    deviceSize["width"] = geometry.pixelWidth;
    deviceSize["height"] = geometry.pixelHeight;

    QJsonObject boundingBox;
    boundingBox["x"] = geometry.sourceRect.x();
    boundingBox["y"] = geometry.sourceRect.y();
    boundingBox["width"] = geometry.sourceRect.width();
    boundingBox["height"] = geometry.sourceRect.height();

    QJsonObject payload;
    payload["op"] = QStringLiteral("export.scene");
    payload["status"] = QStringLiteral("ok");
    payload["path"] = pathInfo.absoluteFilePath();
    payload["format"] = formatToCanonicalString(format);
    payload["deviceSize"] = deviceSize;
    payload["boundingBox"] = boundingBox;

    return ActionResult::success(payload);
}
