//---------------------------------------------------------------------------------------------------------------------
//  @file   tst_action_engine_batches.h
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

#ifndef TST_ACTION_ENGINE_BATCHES_H // Include guard start, prevents this header being processed twice in one translation unit.
#define TST_ACTION_ENGINE_BATCHES_H // Marks TST_ACTION_ENGINE_BATCHES_H as defined for the remainder of the include guard.

#include <QObject> // Provides QObject, the base class QTest requires for a test class.

// Runs full JSON action batches (fixtures/action_batches/*.json) through ActionEngine::run()
// against a hand-built VContainer, and compares the result against the matching
// fixtures/expected/*.expected.json file. Exercises Phase 4's JSON-facing surface: the
// "pattern.resolveName" op and ActionEngine's structured ActionResolverError serialization, on
// top of the pure-C++ NameResolver coverage in tst_name_resolver.cpp.
class TST_ActionEngineBatches : public QObject
{
    Q_OBJECT // Enables QTest's slot discovery and signal/slot support for this class.
public:
    explicit TST_ActionEngineBatches(QObject *parent = nullptr); // Trivial constructor; all state is built per-test below.

private slots:
    void testResolveExisting();        // 01_resolve_existing: a single successful pattern.resolveName.
    void testResolveMissing();         // 02_resolve_missing: a single failing pattern.resolveName, structured error shape.
    void testMultiActionChain();       // 03_multi_action_chain: three actions in one batch, ordered per-action results.
    void testDuplicateNameGuard();     // 04_duplicate_name_guard: resolving the same name twice is idempotent and stable.
    void testEngineSurvivesResolverFailure(); // Regression: a resolver failure mid-batch does not abort later actions or crash the process.
};

#endif // TST_ACTION_ENGINE_BATCHES_H // End of include guard started above.
