//---------------------------------------------------------------------------------------------------------------------
//  @file   tst_action_engine_batches.cpp
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

#include "tst_action_engine_batches.h" // Brings in the TST_ActionEngineBatches class declaration this file implements.

#include "../../libs/actionlayer/action_context.h"  // Brings in ActionContext, bundling scene/doc/data for the engine.
#include "../../libs/actionlayer/action_registry.h" // Brings in ActionRegistry, auto-registering "pattern.resolveName" alongside every other built-in handler.
#include "../../libs/actionlayer/action_engine.h"    // Brings in ActionEngine::run(), the entry point under test.

#include "../../libs/vpatterndb/vcontainer.h" // Brings in VContainer, holding the hand-built points for these tests.
#include "../../libs/vgeometry/vpointf.h"     // Brings in VPointF, the point objects added to the container below.
#include "../../libs/vmisc/def.h"             // Brings in the Unit enum used below.
#include "../../libs/ifc/exception/vexceptionbadid.h" // Brings in VExceptionBadId, thrown by the throwaway handler in testEngineCatchesEscapedVException().

#include "test_pattern_doc.h" // Brings in TestPatternDoc, the minimal VAbstractPattern stub shared by every ActionLayerTest file.

#include <QFile>           // Provides QFile, used to read each fixture/expected JSON file from disk.
#include <QJsonArray>       // Provides QJsonArray, used to build the inline two-action script in the regression test.
#include <QJsonObject>      // Provides QJsonObject, used for each inline action entry in the regression test.
#include <QJsonParseError>  // Provides QJsonParseError, used to detect malformed fixture JSON early and clearly.
#include <QtTest>           // Provides QCOMPARE/QVERIFY, QFINDTESTDATA, and the QTest infrastructure this file's slots run under.
#include <stdexcept>        // Provides std::runtime_error, thrown by the throwaway handler in testEngineCatchesEscapedStdException().

namespace
{
    // Reads and parses one JSON fixture file, located relative to this source file via
    // QFINDTESTDATA so the lookup works the same way whether the test binary runs from an
    // in-source or a shadow (out-of-tree) build directory.
    QJsonDocument loadJsonFixture(const QString &relativePath)
    {
        const QString absolutePath = QFINDTESTDATA(relativePath); // Resolves relative to this .cpp's own source directory.
        Q_ASSERT_X(!absolutePath.isEmpty(), "loadJsonFixture",
                   qPrintable(QStringLiteral("QFINDTESTDATA could not locate fixture: %1").arg(relativePath)));

        QFile file(absolutePath); // The resolved fixture file.
        const bool opened = file.open(QIODevice::ReadOnly | QIODevice::Text); // Fixture files are small; read in one shot below.
        Q_ASSERT_X(opened, "loadJsonFixture", qPrintable(QStringLiteral("failed to open fixture: %1").arg(absolutePath)));

        QJsonParseError parseError; // Populated by QJsonDocument::fromJson() below on malformed input.
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError); // Parses the whole file at once.
        Q_ASSERT_X(parseError.error == QJsonParseError::NoError, "loadJsonFixture",
                   qPrintable(QStringLiteral("invalid JSON in %1: %2").arg(absolutePath, parseError.errorString())));

        return document; // Return the parsed document by value.
    }

    // Populates a fresh VContainer with the three named points every fixture/expected pair below
    // assumes. The ids here are not arbitrary -- they are hardcoded to match the "id" fields
    // baked into fixtures/expected/*.expected.json, so this function and those files must stay
    // in sync if either changes.
    void buildFixtureContainer(VContainer &data)
    {
        data.UpdateGObject(1, new VPointF(0.0, 0.0, QStringLiteral("A"), 0.0, 0.0));    // id 1, name "A".
        data.UpdateGObject(2, new VPointF(10.0, 20.0, QStringLiteral("A1"), 0.0, 0.0)); // id 2, name "A1".
        data.UpdateGObject(3, new VPointF(30.0, 5.0, QStringLiteral("A2"), 0.0, 0.0));  // id 3, name "A2".
    }

    // Runs fixtures/action_batches/<baseName>.json through a freshly-built ActionEngine/VContainer
    // and returns the resulting JSON document, for comparison against the matching expected file.
    QJsonDocument runFixtureBatch(const QString &baseName)
    {
        const Unit unit = Unit::Cm;      // Working unit for the hand-built container; value is arbitrary but must outlive the container.
        VContainer data(nullptr, &unit); // No translator needed; these actions never format a formula.
        buildFixtureContainer(data);     // Populate the three known points every fixture assumes.

        TestPatternDoc doc; // Unused by pattern.resolveName, but ActionContext still requires one.

        ActionContext ctx(nullptr, &doc, &data); // No scene needed: pattern.resolveName never touches ctx.scene().
        ActionRegistry registry;                 // Auto-registers pattern.resolveName alongside every other built-in handler.
        ActionEngine engine(registry);           // Dispatches through the registry above.

        const QJsonDocument script = loadJsonFixture(QStringLiteral("fixtures/action_batches/%1.json").arg(baseName)); // The input script under test.
        return engine.run(script, ctx); // Runs every action in the script; returns {"results": [...]}.
    }
}

// Trivial constructor; every test slot builds its own container/context from scratch.
TST_ActionEngineBatches::TST_ActionEngineBatches(QObject *parent)
    : QObject(parent) // Forwards straight to QObject's constructor.
{
}

// 01_resolve_existing: a single "pattern.resolveName" for a name that exists must succeed with
// the expected {"name", "id", "type"} payload.
void TST_ActionEngineBatches::testResolveExisting()
{
    const QJsonDocument actual = runFixtureBatch(QStringLiteral("01_resolve_existing"));
    const QJsonDocument expected = loadJsonFixture(QStringLiteral("fixtures/expected/01_resolve_existing.expected.json"));
    QCOMPARE(actual, expected);
}

// 02_resolve_missing: a single "pattern.resolveName" for a name that does not exist must produce
// the structured {"type": "nameResolution", ...} error shape, with knownNames matching the
// fixture's actual object names exactly.
void TST_ActionEngineBatches::testResolveMissing()
{
    const QJsonDocument actual = runFixtureBatch(QStringLiteral("02_resolve_missing"));
    const QJsonDocument expected = loadJsonFixture(QStringLiteral("fixtures/expected/02_resolve_missing.expected.json"));
    QCOMPARE(actual, expected);
}

// 03_multi_action_chain: three actions in one batch must produce three per-action results, in
// input order, each independently correct.
void TST_ActionEngineBatches::testMultiActionChain()
{
    const QJsonDocument actual = runFixtureBatch(QStringLiteral("03_multi_action_chain"));
    const QJsonDocument expected = loadJsonFixture(QStringLiteral("fixtures/expected/03_multi_action_chain.expected.json"));
    QCOMPARE(actual, expected);
}

// 04_duplicate_name_guard: resolving the same existing name twice in one batch must be
// idempotent -- both results identical and correct. True duplicate-name-at-creation-time
// rejection is enforced by VContainer::IsUnique()/AddGObject() itself once Phase 5's mutating
// handlers exist; that gets its own test then. Phase 4 has no creation handlers, so this file is
// scoped to what it can actually exercise today: read-only resolution is stable under repetition.
void TST_ActionEngineBatches::testDuplicateNameGuard()
{
    const QJsonDocument actual = runFixtureBatch(QStringLiteral("04_duplicate_name_guard"));
    const QJsonDocument expected = loadJsonFixture(QStringLiteral("fixtures/expected/04_duplicate_name_guard.expected.json"));
    QCOMPARE(actual, expected);
}

// Regression test: a resolver failure partway through a batch must not abort the script or crash
// the process -- ActionEngine::run() must catch ActionResolverError itself and keep dispatching
// later actions, returning a well-formed result for each.
void TST_ActionEngineBatches::testEngineSurvivesResolverFailure()
{
    const Unit unit = Unit::Cm;      // Working unit for the hand-built container.
    VContainer data(nullptr, &unit); // No translator needed; these actions never format a formula.
    buildFixtureContainer(data);     // Populate the three known points; only "A1" is used below.

    TestPatternDoc doc; // Unused by pattern.resolveName, but ActionContext still requires one.

    ActionContext ctx(nullptr, &doc, &data); // No scene needed: pattern.resolveName never touches ctx.scene().
    ActionRegistry registry;                 // Auto-registers pattern.resolveName alongside every other built-in handler.
    ActionEngine engine(registry);           // Dispatches through the registry above.

    // Deliberately not loaded from a fixture file: this script exists purely to prove ordering
    // and survival across a failure, not to exercise a specific documented input/output pair.
    const QJsonObject missingAction{{"op", QStringLiteral("pattern.resolveName")}, {"name", QStringLiteral("NoSuchName")}};
    const QJsonObject validAction{{"op", QStringLiteral("pattern.resolveName")}, {"name", QStringLiteral("A1")}};
    const QJsonDocument script(QJsonObject{{"actions", QJsonArray{missingAction, validAction}}});

    const QJsonDocument output = engine.run(script, ctx); // Must return normally: ActionResolverError is caught inside run() itself.
    const QJsonArray results = output.object().value("results").toArray();
    QCOMPARE(results.size(), 2); // Both actions must produce a result; the first one failing must not abort the second.

    const QJsonObject firstResult = results.at(0).toObject();
    QVERIFY2(!firstResult.value("ok").toBool(), "the first action (unknown name) must report ok == false, not crash the batch");
    QCOMPARE(firstResult.value("error").toObject().value("type").toString(), QStringLiteral("nameResolution"));

    const QJsonObject secondResult = results.at(1).toObject();
    QVERIFY2(secondResult.value("ok").toBool(), "the second action (known name) must still succeed after the first one failed");
}

// Phase 10: "op" not present in ActionRegistry must fail cleanly (ActionEngine::run()'s own
// !handler guard), not be silently skipped and not crash. This is the one error path already
// covered by production code before Phase 10 -- this slot just locks it in with a real assertion,
// since nothing at the C++ level exercised it before.
void TST_ActionEngineBatches::testUnknownOpDoesNotCrash()
{
    const Unit unit = Unit::Cm;
    VContainer data(nullptr, &unit);
    TestPatternDoc doc;
    ActionContext ctx(nullptr, &doc, &data); // Never touched: the unknown-op path returns before any handler runs.
    ActionRegistry registry;
    ActionEngine engine(registry);

    const QJsonObject action{{"op", QStringLiteral("notARealTool")}};
    const QJsonDocument script(QJsonObject{{"actions", QJsonArray{action}}});

    const QJsonDocument output = engine.run(script, ctx); // Must return normally.
    const QJsonArray results = output.object().value("results").toArray();
    QCOMPARE(results.size(), 1);

    const QJsonObject result = results.at(0).toObject();
    QVERIFY2(!result.value("ok").toBool(), "an unregistered op must report ok == false");
    QVERIFY2(result.value("error").toString().contains(QStringLiteral("notARealTool")),
              "the error message should name the offending op so a caller can self-correct");
}

// Phase 10: a registered, real op ("line") missing a required field ("secondPoint") must fail
// before ever reaching NameResolver or VToolLine::Create() -- line_handlers.cpp validates both
// names are non-empty first. Locks in that this schema-style validation already happens and
// produces a normal failed result, not a crash from an empty-string name reaching Create().
void TST_ActionEngineBatches::testMissingRequiredFieldReturnsFailure()
{
    const Unit unit = Unit::Cm;
    VContainer data(nullptr, &unit);
    TestPatternDoc doc;
    ActionContext ctx(nullptr, &doc, &data); // scene left null: the missing-field check fires before "line" would ever touch ctx.scene().
    ActionRegistry registry;
    ActionEngine engine(registry);

    const QJsonObject action{{"op", QStringLiteral("line")}, {"firstPoint", QStringLiteral("A")}}; // "secondPoint" deliberately omitted.
    const QJsonDocument script(QJsonObject{{"actions", QJsonArray{action}}});

    const QJsonDocument output = engine.run(script, ctx);
    const QJsonArray results = output.object().value("results").toArray();
    QCOMPARE(results.size(), 1);

    const QJsonObject result = results.at(0).toObject();
    QVERIFY2(!result.value("ok").toBool(), "line missing \"secondPoint\" must fail, not crash or silently default");
    QVERIFY2(result.value("error").toString().contains(QStringLiteral("secondPoint")),
              "the error message should name the missing field");
}

// Phase 10: proves ActionEngine::run()'s VException safety net (action_engine.cpp), not any
// existing handler's own local try/catch -- every real op already catches VException itself
// before it would reach this loop (see e.g. formula_point_handlers.cpp's runCreate()). Registers a
// throwaway handler that throws directly, bypassing every real op's own protection, so what's
// actually under test is run()'s own catch (const VException&) clause.
void TST_ActionEngineBatches::testEngineCatchesEscapedVException()
{
    const Unit unit = Unit::Cm;
    VContainer data(nullptr, &unit);
    buildFixtureContainer(data); // Populates "A"/"A1"/"A2" so the follow-up action below can succeed.
    TestPatternDoc doc;
    ActionContext ctx(nullptr, &doc, &data);

    ActionRegistry registry; // Every built-in handler, plus...
    registry.registerAction(QStringLiteral("test.throwsVException"),
        [](const QJsonObject &, const ActionContext &) -> ActionResult {
            throw VExceptionBadId(QStringLiteral("test-induced failure"), quint32(999)); // Deliberately uncaught here.
        });
    ActionEngine engine(registry);

    const QJsonObject failingAction{{"op", QStringLiteral("test.throwsVException")}};
    const QJsonObject followupAction{{"op", QStringLiteral("pattern.resolveName")}, {"name", QStringLiteral("A")}};
    const QJsonDocument script(QJsonObject{{"actions", QJsonArray{failingAction, followupAction}}});

    const QJsonDocument output = engine.run(script, ctx); // Must return normally: the VException must not escape run() itself.
    const QJsonArray results = output.object().value("results").toArray();
    QCOMPARE(results.size(), 2); // Both actions must produce a result; the escaped exception must not abort the batch.

    const QJsonObject firstResult = results.at(0).toObject();
    QVERIFY2(!firstResult.value("ok").toBool(), "the throwaway handler's VException must surface as ok == false, not crash the process");
    QCOMPARE(firstResult.value("error").toObject().value("type").toString(), QStringLiteral("coreException"));
    QVERIFY2(!firstResult.value("error").toObject().value("message").toString().isEmpty(),
              "VException::ErrorMessage() should be carried through, not dropped");

    const QJsonObject secondResult = results.at(1).toObject();
    QVERIFY2(secondResult.value("ok").toBool(), "the second action must still run after the first escaped an exception");
}

// Same as testEngineCatchesEscapedVException(), for a plain std::exception -- exercises run()'s
// catch (const std::exception&) clause ("unhandledException") instead of the VException one.
void TST_ActionEngineBatches::testEngineCatchesEscapedStdException()
{
    const Unit unit = Unit::Cm;
    VContainer data(nullptr, &unit);
    TestPatternDoc doc;
    ActionContext ctx(nullptr, &doc, &data);

    ActionRegistry registry;
    registry.registerAction(QStringLiteral("test.throwsStdException"),
        [](const QJsonObject &, const ActionContext &) -> ActionResult {
            throw std::runtime_error("plain std::exception, not a VException"); // Deliberately uncaught here.
        });
    ActionEngine engine(registry);

    const QJsonObject action{{"op", QStringLiteral("test.throwsStdException")}};
    const QJsonDocument script(QJsonObject{{"actions", QJsonArray{action}}});

    const QJsonDocument output = engine.run(script, ctx); // Must return normally.
    const QJsonArray results = output.object().value("results").toArray();
    QCOMPARE(results.size(), 1);

    const QJsonObject result = results.at(0).toObject();
    QVERIFY2(!result.value("ok").toBool(), "the throwaway handler's std::exception must surface as ok == false, not crash the process");
    QCOMPARE(result.value("error").toObject().value("type").toString(), QStringLiteral("unhandledException"));
    QCOMPARE(result.value("error").toObject().value("message").toString(), QStringLiteral("plain std::exception, not a VException"));
}
