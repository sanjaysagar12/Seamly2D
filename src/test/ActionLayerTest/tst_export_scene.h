//---------------------------------------------------------------------------------------------------------------------
//  @file   tst_export_scene.h
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

#ifndef TST_EXPORT_SCENE_H // Include guard start, prevents this header being processed twice in one translation unit.
#define TST_EXPORT_SCENE_H // Marks TST_EXPORT_SCENE_H as defined for the remainder of the include guard.

#include <QObject> // Provides QObject, the base class QTest requires for a test class.

// Exercises ActionEngine end-to-end against the "export.scene" handler (Phase A: direct scene
// export), using the same hand-built VMainGraphicsScene + manually-added QGraphicsLineItems
// pattern tst_render_snapshot.cpp already established -- export.scene only reads the scene's
// items/bounding rect, so it doesn't need a fully parsed pattern either.
class TST_ExportScene : public QObject
{
    Q_OBJECT // Enables QTest's slot discovery and signal/slot support for this class.
public:
    explicit TST_ExportScene(QObject *parent = nullptr); // Trivial constructor; all state is built per-test below.

private slots:
    void testExportRasterFormats();          // Verifies png/jpg/bmp/ppm/tif each round-trip to a loadable image matching the reported deviceSize.
    void testExportSvg();                    // Verifies svg writes well-formed XML with an <svg> root.
    void testExportPdf();                    // Verifies pdf writes a file starting with the "%PDF" magic bytes.
    void testExportFlatDxfAsciiVsBinary();    // Verifies binaryDXF:true vs. false produce different, both non-empty, byte content for the same DXF export.
    void testExportMissingPathFails();        // Verifies an empty "path" reports a structured error, matching render.snapshot's own check.
    void testExportEmptySceneFails();         // Verifies an empty scene reports a structured error and writes no file.
    void testExportAmbiguousDxfExtensionFails(); // Verifies a bare ".dxf" path with no explicit "format" reports a structured error instead of guessing a version.
    void testExportUnsupportedFormatFails();  // Verifies an unrecognized "format" string reports a structured error listing the real set.
    void testExportPsTolerantOfMissingPdftops(); // Verifies ps/eps export never crashes/hangs: either it succeeds (pdftops present) or fails cleanly (absent).
};

#endif // TST_EXPORT_SCENE_H // End of include guard started above.
