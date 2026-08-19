//---------------------------------------------------------------------------------------------------------------------
//  @file   tst_pattern_dump.h
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

#ifndef TST_PATTERN_DUMP_H // Include guard start, prevents this header being processed twice in one translation unit.
#define TST_PATTERN_DUMP_H // Marks TST_PATTERN_DUMP_H as defined for the remainder of the include guard.

#include <QObject> // Provides QObject, the base class QTest requires for a test class.

// Exercises ActionEngine end-to-end against Phase 1's three read-only handlers
// (pattern.dump, pattern.listMeasurements, pattern.listTools), using a hand-built
// VContainer/VAbstractPattern pair rather than loading a pattern file from disk --
// VPattern (the class that parses .val/.sm2d files) lives in the seamly2d app target,
// not in any static library this test can link against.
class TST_PatternDump : public QObject
{
    Q_OBJECT // Enables QTest's slot discovery and signal/slot support for this class.
public:
    explicit TST_PatternDump(QObject *parent = nullptr); // Trivial constructor; all state is built per-test below.

private slots:
    void testPatternDump();           // Verifies "pattern.dump" reports every hand-added point and history entry.
    void testListMeasurementsEmpty(); // Verifies "pattern.listMeasurements" succeeds with an empty array, not an error.
    void testListTools();             // Verifies "pattern.listTools" reports exactly the three Phase 1 op names.
};

#endif // TST_PATTERN_DUMP_H // End of include guard started above.
