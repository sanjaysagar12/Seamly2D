//---------------------------------------------------------------------------------------------------------------------
//  @file   tst_action_schema.h
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

#ifndef TST_ACTION_SCHEMA_H // Include guard start, prevents this header being processed twice in one translation unit.
#define TST_ACTION_SCHEMA_H // Marks TST_ACTION_SCHEMA_H as defined for the remainder of the include guard.

#include <QObject> // Provides QObject, the base class QTest requires for a test class.

// Regression-proofs the `actiond --list-tools` command's underlying data (action_registry.cpp's
// registerBuiltinActions(), which pairs every handler with an ActionSchema at its own registration
// call) against future drift: if a new action is ever registered without an ActionSchema
// (impossible today only because ActionRegistry::registerAction() requires one -- see
// action_registry.h), one of these tests fails loudly instead of the tool list quietly going stale
// the way pattern_list_tools_handler.cpp's separate hand-written list once did (see
// docs/action-layer-schema.md's own "Findings" section).
//
// Both slots below are pure in-process checks against a freshly constructed ActionRegistry -- no
// subprocess involved. The complementary "is `actiond --list-tools --format=ai`'s actual CLI output valid,
// non-empty JSON" check lives in tests/actionlayer/run_batch (see its own main.cpp), which already
// reliably spawns actiond.exe as a subprocess for exactly this kind of CLI-surface check; adding a
// second, independent subprocess-spawning path here would duplicate that coverage without adding
// anything this file's own in-process checks don't already guarantee about the registry itself.
class TST_ActionSchema : public QObject
{
    Q_OBJECT // Enables QTest's slot discovery and signal/slot support for this class.
public:
    explicit TST_ActionSchema(QObject *parent = nullptr); // Trivial constructor; all state is built per-test below.

private slots:
    void testEveryActionHasExactlyOneSchema(); // allSchemas().size() == registry.actionCount(), no duplicate op names, no missing hasAction() entry.
    void testEverySchemaIsWellFormed();        // Every schema has a non-empty op/category/description/exampleRequest, and every required param has no default.
};

#endif // TST_ACTION_SCHEMA_H // End of include guard started above.
