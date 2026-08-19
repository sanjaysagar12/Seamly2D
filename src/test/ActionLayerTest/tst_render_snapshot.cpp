//---------------------------------------------------------------------------------------------------------------------
//  @file   tst_render_snapshot.cpp
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

#include "tst_render_snapshot.h" // Brings in the TST_RenderSnapshot class declaration this file implements.

#include "../../libs/actionlayer/action_context.h"  // Brings in ActionContext, bundling scene/doc/data for the engine.
#include "../../libs/actionlayer/action_registry.h" // Brings in ActionRegistry, auto-registering render.snapshot alongside the Phase 1 handlers.
#include "../../libs/actionlayer/action_engine.h"    // Brings in ActionEngine::run(), the entry point under test.

#include "../../libs/vwidgets/vmaingraphicsscene.h" // Brings in VMainGraphicsScene, the scene type render.snapshot renders.

#include <QFileInfo>         // Provides QFileInfo, used to check the output file exists and is non-empty.
#include <QGraphicsLineItem> // Provides QGraphicsLineItem, the manually-added scene content for these tests.
#include <QImage>            // Provides QImage, used to load the saved file back and compare its actual dimensions.
#include <QJsonArray>        // Provides QJsonArray, used to build the "actions" script array and read "results".
#include <QJsonObject>       // Provides QJsonObject, used for each action entry and result record.
#include <QTemporaryDir>     // Provides QTemporaryDir, giving each test its own auto-cleaned output directory.
#include <QtTest>            // Provides QCOMPARE/QVERIFY and the QTest infrastructure this file's slots run under.

// Trivial constructor; every test slot builds its own scene/context from scratch.
TST_RenderSnapshot::TST_RenderSnapshot(QObject *parent)
    : QObject(parent) // Forwards straight to QObject's constructor.
{
}

// Verifies "render.snapshot" against a scene with real content: the handler must report
// status "ok", the file it names must actually exist and be non-empty, and the pixelSize it
// reports must match the dimensions of the image actually written to disk.
void TST_RenderSnapshot::testRenderSnapshotSuccess()
{
    VMainGraphicsScene scene; // Stand-in draft scene; render.snapshot only reads items()/itemsBoundingRect(), so no real tool parsing is needed.
    scene.addItem(new QGraphicsLineItem(0, 0, 100, 50));   // First manually-added line; scene takes ownership and deletes it with the scene.
    scene.addItem(new QGraphicsLineItem(20, 80, 120, 10)); // Second manually-added line, widening the scene's bounding rect on both axes.

    ActionContext ctx(&scene, nullptr, nullptr); // No doc/data needed: this test passes no "highlight" names, so ctx.data() is never touched.
    ActionRegistry registry;                     // Auto-registers render.snapshot alongside pattern.dump/listMeasurements/listTools.
    ActionEngine engine(registry);               // Dispatches through the registry above.

    QTemporaryDir tempDir; // Auto-cleaned output directory; avoids leaving test artifacts behind on disk.
    QVERIFY2(tempDir.isValid(), "failed to create a temp directory for the render.snapshot test"); // Guards the path below.
    const QString outputPath = tempDir.filePath(QStringLiteral("snapshot.png")); // Output path; extension drives the default format (PNG).

    const QJsonObject action{{"op", QStringLiteral("render.snapshot")}, {"path", outputPath}}; // Minimal action: only the required "path" is set, exercising every other param's default.
    const QJsonDocument script(QJsonObject{{"actions", QJsonArray{action}}}); // Wraps it in the engine's expected script shape.

    const QJsonDocument output = engine.run(script, ctx); // Runs the script; should dispatch to handleRenderSnapshot().
    const QJsonArray results = output.object().value("results").toArray(); // One result per input action.
    QCOMPARE(results.size(), 1); // Exactly one action was submitted, so exactly one result is expected back.

    const QJsonObject result = results.first().toObject(); // The (only) result entry for the render action.
    QVERIFY2(result.value("ok").toBool(),
             qPrintable(QStringLiteral("render.snapshot must succeed against a populated scene: %1").arg(result.value("error").toString()))); // Includes the actual error text if this fails, for a debuggable test.

    const QJsonObject payload = result.value("value").toObject(); // The handler's documented success payload.
    QCOMPARE(payload.value("status").toString(), QStringLiteral("ok")); // Payload's own "status" field, in addition to the envelope's "ok".

    QVERIFY2(QFileInfo::exists(outputPath), "render.snapshot must write the output file it names"); // The file must actually land on disk.
    const QFileInfo savedFileInfo(outputPath); // Reused below for the size check.
    QVERIFY2(savedFileInfo.size() > 0, "render.snapshot must not write an empty (zero-byte) file"); // A 0-byte file would mean save() silently produced garbage.

    const QImage savedImage(outputPath); // Load the file back to compare against the handler's *reported* pixelSize.
    QVERIFY2(!savedImage.isNull(), "the file render.snapshot wrote must be a loadable image"); // Guards the size comparison below.

    const QJsonObject pixelSize = payload.value("pixelSize").toObject(); // The handler's reported dimensions.
    QCOMPARE(pixelSize.value("width").toInt(), savedImage.width());   // Reported width must match the actual saved image.
    QCOMPARE(pixelSize.value("height").toInt(), savedImage.height()); // Reported height must match the actual saved image.
}

// Verifies "render.snapshot" against a scene with zero items reports a structured error
// (not a crash, and not a degenerate image) and does not create the requested output file.
void TST_RenderSnapshot::testRenderSnapshotEmptyScene()
{
    VMainGraphicsScene scene; // Deliberately left empty: no items added.

    ActionContext ctx(&scene, nullptr, nullptr); // Same no-doc/data setup as the success test above; irrelevant to this failure path.
    ActionRegistry registry;                     // Auto-registers render.snapshot alongside the other built-in handlers.
    ActionEngine engine(registry);               // Dispatches through the registry above.

    QTemporaryDir tempDir; // Auto-cleaned output directory, even though nothing should be written into it.
    QVERIFY2(tempDir.isValid(), "failed to create a temp directory for the render.snapshot empty-scene test"); // Guards the path below.
    const QString outputPath = tempDir.filePath(QStringLiteral("should_not_exist.png")); // Named so a false-positive write is easy to spot.

    const QJsonObject action{{"op", QStringLiteral("render.snapshot")}, {"path", outputPath}}; // Same minimal action shape as the success test.
    const QJsonDocument script(QJsonObject{{"actions", QJsonArray{action}}}); // Wraps it in the engine's expected script shape.

    const QJsonDocument output = engine.run(script, ctx); // Runs the script; should dispatch to handleRenderSnapshot() and fail cleanly.
    const QJsonArray results = output.object().value("results").toArray(); // One result per input action.
    QCOMPARE(results.size(), 1); // Exactly one action was submitted, so exactly one result is expected back.

    const QJsonObject result = results.first().toObject(); // The (only) result entry for the render action.
    QVERIFY2(!result.value("ok").toBool(), "render.snapshot against an empty scene must report an error, not succeed"); // Must not silently "succeed" with a degenerate image.
    QVERIFY2(!result.value("error").toString().isEmpty(), "the error result must carry a non-empty, actionable message"); // A blank error would not be actionable for a caller.

    QVERIFY2(!QFileInfo::exists(outputPath), "render.snapshot must not create a file when it fails"); // No partial/garbage file should be left behind on the error path.
}
