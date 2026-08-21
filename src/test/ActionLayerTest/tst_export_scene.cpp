//---------------------------------------------------------------------------------------------------------------------
//  @file   tst_export_scene.cpp
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

#include "tst_export_scene.h" // Brings in the TST_ExportScene class declaration this file implements.

#include "../../libs/actionlayer/action_context.h"  // Brings in ActionContext, bundling scene/doc/data for the engine.
#include "../../libs/actionlayer/action_registry.h" // Brings in ActionRegistry, auto-registering export.scene alongside every other built-in handler.
#include "../../libs/actionlayer/action_engine.h"    // Brings in ActionEngine::run(), the entry point under test.

#include "../../libs/vwidgets/vmaingraphicsscene.h" // Brings in VMainGraphicsScene, the scene type export.scene reads.

#include <QDomDocument>      // Provides QDomDocument, used to confirm the SVG output is well-formed XML with an <svg> root.
#include <QFile>             // Provides QFile, used to read back raw bytes for the PDF-magic and DXF-content checks.
#include <QFileInfo>         // Provides QFileInfo, used to check output files exist and are non-empty.
#include <QGraphicsLineItem> // Provides QGraphicsLineItem, the manually-added scene content for these tests.
#include <QImage>            // Provides QImage, used to load raster output back and compare its actual dimensions.
#include <QJsonArray>        // Provides QJsonArray, used to build the "actions" script array and read "results".
#include <QJsonObject>       // Provides QJsonObject, used for each action entry and result record.
#include <QTemporaryDir>     // Provides QTemporaryDir, giving each test its own auto-cleaned output directory.
#include <QtTest>            // Provides QCOMPARE/QVERIFY and the QTest infrastructure this file's slots run under.

namespace
{
    // Builds a scene with real (non-degenerate-bounding-box) content, matching
    // tst_render_snapshot.cpp's own fixture -- export.scene only reads items()/itemsBoundingRect(),
    // so no real tool parsing is needed here either.
    void addSampleContent(VMainGraphicsScene &scene)
    {
        scene.addItem(new QGraphicsLineItem(0, 0, 100, 50));   // Scene takes ownership and deletes it with the scene.
        scene.addItem(new QGraphicsLineItem(20, 80, 120, 10)); // Widens the scene's bounding rect on both axes.
    }

    // Runs a single "export.scene" action and returns its (only) result object, asserting the
    // engine itself produced exactly one result -- shared by every slot below so each test body
    // only has to state its own format-specific args and assertions.
    QJsonObject runExportScene(VMainGraphicsScene &scene, const QJsonObject &extraArgs)
    {
        ActionContext ctx(&scene, nullptr, nullptr); // No doc/data needed: export.scene never resolves names.
        ActionRegistry registry;                     // Auto-registers export.scene alongside every other built-in handler.
        ActionEngine engine(registry);                // Dispatches through the registry above.

        QJsonObject action = extraArgs;
        action[QStringLiteral("op")] = QStringLiteral("export.scene");
        const QJsonDocument script(QJsonObject{{"actions", QJsonArray{action}}});

        const QJsonDocument output = engine.run(script, ctx);
        const QJsonArray results = output.object().value("results").toArray();
        return results.size() == 1 ? results.first().toObject() : QJsonObject();
    }
}

TST_ExportScene::TST_ExportScene(QObject *parent)
    : QObject(parent)
{
}

// Verifies every raster format export.scene shares with render.snapshot (png/jpg/bmp/ppm/tif)
// round-trips to a loadable image whose actual dimensions match the handler's reported deviceSize.
void TST_ExportScene::testExportRasterFormats()
{
    const QStringList formats{
        QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("bmp"), QStringLiteral("ppm"), QStringLiteral("tif")};

    for (const QString &format : formats)
    {
        VMainGraphicsScene scene;
        addSampleContent(scene);

        QTemporaryDir tempDir;
        QVERIFY2(tempDir.isValid(), "failed to create a temp directory for the export.scene raster test");
        const QString outputPath = tempDir.filePath(QStringLiteral("snapshot.") + format);

        const QJsonObject result = runExportScene(scene, QJsonObject{{"path", outputPath}, {"format", format}});
        QVERIFY2(result.value("ok").toBool(),
                 qPrintable(QStringLiteral("export.scene must succeed for format '%1': %2").arg(format, result.value("error").toString())));

        const QJsonObject payload = result.value("value").toObject();
        QCOMPARE(payload.value("status").toString(), QStringLiteral("ok"));
        QCOMPARE(payload.value("format").toString(), format);

        QVERIFY2(QFileInfo::exists(outputPath), qPrintable(QStringLiteral("export.scene must write the output file for format '%1'").arg(format)));
        const QImage savedImage(outputPath);
        QVERIFY2(!savedImage.isNull(), qPrintable(QStringLiteral("the file export.scene wrote for format '%1' must be a loadable image").arg(format)));

        const QJsonObject deviceSize = payload.value("deviceSize").toObject();
        QCOMPARE(deviceSize.value("width").toInt(), savedImage.width());
        QCOMPARE(deviceSize.value("height").toInt(), savedImage.height());
    }
}

// Verifies "export.scene" with format "svg" writes well-formed XML with an <svg> root element.
void TST_ExportScene::testExportSvg()
{
    VMainGraphicsScene scene;
    addSampleContent(scene);

    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "failed to create a temp directory for the export.scene svg test");
    const QString outputPath = tempDir.filePath(QStringLiteral("drawing.svg"));

    const QJsonObject result = runExportScene(scene, QJsonObject{{"path", outputPath}});
    QVERIFY2(result.value("ok").toBool(),
             qPrintable(QStringLiteral("export.scene must succeed for svg (inferred from extension): %1").arg(result.value("error").toString())));

    QVERIFY2(QFileInfo::exists(outputPath), "export.scene must write the output file for svg");
    const QFileInfo savedFileInfo(outputPath);
    QVERIFY2(savedFileInfo.size() > 0, "export.scene must not write an empty (zero-byte) svg file");

    QFile svgFile(outputPath);
    QVERIFY2(svgFile.open(QIODevice::ReadOnly), "must be able to reopen the svg file export.scene wrote");
    QDomDocument doc;
    QVERIFY2(doc.setContent(&svgFile), "export.scene's svg output must be well-formed XML");
    QCOMPARE(doc.documentElement().tagName(), QStringLiteral("svg"));
}

// Verifies "export.scene" with format "pdf" writes a file starting with the standard PDF magic bytes.
void TST_ExportScene::testExportPdf()
{
    VMainGraphicsScene scene;
    addSampleContent(scene);

    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "failed to create a temp directory for the export.scene pdf test");
    const QString outputPath = tempDir.filePath(QStringLiteral("drawing.pdf"));

    const QJsonObject result = runExportScene(scene, QJsonObject{{"path", outputPath}, {"format", QStringLiteral("pdf")}});
    QVERIFY2(result.value("ok").toBool(),
             qPrintable(QStringLiteral("export.scene must succeed for pdf: %1").arg(result.value("error").toString())));

    QFile pdfFile(outputPath);
    QVERIFY2(pdfFile.open(QIODevice::ReadOnly), "must be able to reopen the pdf file export.scene wrote");
    const QByteArray header = pdfFile.read(4);
    QCOMPARE(header, QByteArrayLiteral("%PDF"));
}

// Verifies binaryDXF:true vs. false produce different (and both non-empty) byte content for the
// same scene/format, confirming the flag is actually threaded through to VDxfPaintDevice rather
// than silently ignored.
void TST_ExportScene::testExportFlatDxfAsciiVsBinary()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "failed to create a temp directory for the export.scene dxf test");
    const QString asciiPath = tempDir.filePath(QStringLiteral("drawing_ascii.dxf"));
    const QString binaryPath = tempDir.filePath(QStringLiteral("drawing_binary.dxf"));

    {
        VMainGraphicsScene scene;
        addSampleContent(scene);
        const QJsonObject result = runExportScene(scene,
            QJsonObject{{"path", asciiPath}, {"format", QStringLiteral("dxf-2013")}, {"binaryDXF", false}});
        QVERIFY2(result.value("ok").toBool(),
                 qPrintable(QStringLiteral("export.scene must succeed for ascii dxf-2013: %1").arg(result.value("error").toString())));
    }
    {
        VMainGraphicsScene scene;
        addSampleContent(scene);
        const QJsonObject result = runExportScene(scene,
            QJsonObject{{"path", binaryPath}, {"format", QStringLiteral("dxf-2013")}, {"binaryDXF", true}});
        QVERIFY2(result.value("ok").toBool(),
                 qPrintable(QStringLiteral("export.scene must succeed for binary dxf-2013: %1").arg(result.value("error").toString())));
    }

    QFile asciiFile(asciiPath);
    QFile binaryFile(binaryPath);
    QVERIFY2(asciiFile.open(QIODevice::ReadOnly), "must be able to reopen the ascii dxf file export.scene wrote");
    QVERIFY2(binaryFile.open(QIODevice::ReadOnly), "must be able to reopen the binary dxf file export.scene wrote");

    const QByteArray asciiBytes = asciiFile.readAll();
    const QByteArray binaryBytes = binaryFile.readAll();
    QVERIFY2(!asciiBytes.isEmpty(), "ascii dxf export must not be empty");
    QVERIFY2(!binaryBytes.isEmpty(), "binary dxf export must not be empty");
    QVERIFY2(asciiBytes != binaryBytes, "ascii and binary dxf export of the same scene must differ -- binaryDXF must actually be threaded through");
}

// Verifies an empty "path" reports a structured error, matching render.snapshot's own check.
void TST_ExportScene::testExportMissingPathFails()
{
    VMainGraphicsScene scene;
    addSampleContent(scene);

    const QJsonObject result = runExportScene(scene, QJsonObject{{"path", QString()}});
    QVERIFY2(!result.value("ok").toBool(), "export.scene with an empty path must report an error, not succeed");
    QVERIFY2(!result.value("error").toString().isEmpty(), "the error result must carry a non-empty, actionable message");
}

// Verifies an empty scene reports a structured error and writes no file, matching
// render.snapshot's own empty-scene behavior.
void TST_ExportScene::testExportEmptySceneFails()
{
    VMainGraphicsScene scene; // Deliberately left empty: no items added.

    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "failed to create a temp directory for the export.scene empty-scene test");
    const QString outputPath = tempDir.filePath(QStringLiteral("should_not_exist.svg"));

    const QJsonObject result = runExportScene(scene, QJsonObject{{"path", outputPath}});
    QVERIFY2(!result.value("ok").toBool(), "export.scene against an empty scene must report an error, not succeed");
    QVERIFY2(!result.value("error").toString().isEmpty(), "the error result must carry a non-empty, actionable message");
    QVERIFY2(!QFileInfo::exists(outputPath), "export.scene must not create a file when it fails");
}

// Verifies a bare ".dxf" path with no explicit "format" reports a structured error instead of
// guessing one of the nine AutoCAD versions.
void TST_ExportScene::testExportAmbiguousDxfExtensionFails()
{
    VMainGraphicsScene scene;
    addSampleContent(scene);

    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "failed to create a temp directory for the export.scene ambiguous-dxf test");
    const QString outputPath = tempDir.filePath(QStringLiteral("should_not_exist.dxf"));

    const QJsonObject result = runExportScene(scene, QJsonObject{{"path", outputPath}}); // No "format" given.
    QVERIFY2(!result.value("ok").toBool(), "export.scene with a bare .dxf path and no explicit format must report an error");
    QVERIFY2(!result.value("error").toString().isEmpty(), "the error result must carry a non-empty, actionable message");
    QVERIFY2(!QFileInfo::exists(outputPath), "export.scene must not create a file when it fails");
}

// Verifies an unrecognized "format" string reports a structured error listing the real set,
// rather than silently defaulting.
void TST_ExportScene::testExportUnsupportedFormatFails()
{
    VMainGraphicsScene scene;
    addSampleContent(scene);

    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "failed to create a temp directory for the export.scene unsupported-format test");
    const QString outputPath = tempDir.filePath(QStringLiteral("drawing.out"));

    const QJsonObject result = runExportScene(scene, QJsonObject{{"path", outputPath}, {"format", QStringLiteral("not-a-real-format")}});
    QVERIFY2(!result.value("ok").toBool(), "export.scene with an unrecognized format must report an error");
    QVERIFY2(!result.value("error").toString().isEmpty(), "the error result must carry a non-empty, actionable message");
}

// Verifies ps/eps export never crashes or hangs regardless of whether the external "pdftops" tool
// is available on this machine: either it succeeds (pdftops present) or it fails cleanly with a
// structured error (pdftops absent) -- both are acceptable outcomes for this test.
void TST_ExportScene::testExportPsTolerantOfMissingPdftops()
{
    VMainGraphicsScene scene;
    addSampleContent(scene);

    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "failed to create a temp directory for the export.scene ps test");
    const QString outputPath = tempDir.filePath(QStringLiteral("drawing.ps"));

    const QJsonObject result = runExportScene(scene, QJsonObject{{"path", outputPath}, {"format", QStringLiteral("ps")}});
    if (result.value("ok").toBool())
    {
        QVERIFY2(QFileInfo::exists(outputPath), "export.scene reported success for ps but wrote no file");
    }
    else
    {
        QVERIFY2(!result.value("error").toString().isEmpty(), "a failed ps export must still carry a non-empty, actionable error message");
    }
}
