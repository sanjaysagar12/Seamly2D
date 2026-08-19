//---------------------------------------------------------------------------------------------------------------------
//  @file   tst_render_snapshot.h
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

#ifndef TST_RENDER_SNAPSHOT_H // Include guard start, prevents this header being processed twice in one translation unit.
#define TST_RENDER_SNAPSHOT_H // Marks TST_RENDER_SNAPSHOT_H as defined for the remainder of the include guard.

#include <QObject> // Provides QObject, the base class QTest requires for a test class.

// Exercises ActionEngine end-to-end against the "render.snapshot" handler, using a hand-built
// VMainGraphicsScene with manually-added QGraphicsLineItems rather than real vtools Create()
// output -- render.snapshot only reads the scene's items/bounding rect, so it doesn't need a
// fully parsed pattern the way pattern.dump's history-walking does.
class TST_RenderSnapshot : public QObject
{
    Q_OBJECT // Enables QTest's slot discovery and signal/slot support for this class.
public:
    explicit TST_RenderSnapshot(QObject *parent = nullptr); // Trivial constructor; all state is built per-test below.

private slots:
    void testRenderSnapshotSuccess();    // Verifies a populated scene renders, saves, and reports matching pixelSize.
    void testRenderSnapshotEmptyScene(); // Verifies an empty scene reports a structured error and writes no file.
};

#endif // TST_RENDER_SNAPSHOT_H // End of include guard started above.
