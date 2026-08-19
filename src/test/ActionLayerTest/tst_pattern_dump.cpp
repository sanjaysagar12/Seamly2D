//---------------------------------------------------------------------------------------------------------------------
//  @file   tst_pattern_dump.cpp
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

#include "tst_pattern_dump.h" // Brings in the TST_PatternDump class declaration this file implements.

#include "../../libs/actionlayer/action_context.h"  // Brings in ActionContext, bundling scene/doc/data for the engine.
#include "../../libs/actionlayer/action_registry.h" // Brings in ActionRegistry, auto-registering Phase 1's built-in handlers.
#include "../../libs/actionlayer/action_engine.h"    // Brings in ActionEngine::run(), the entry point under test.

#include "../../libs/vpatterndb/vcontainer.h" // Brings in VContainer, holding the hand-built points for this test.
#include "../../libs/vgeometry/vpointf.h"     // Brings in VPointF, the point objects added to the container below.
#include "../../libs/ifc/xml/vabstractpattern.h" // Brings in VAbstractPattern, the base class the test's stub document extends.
#include "../../libs/ifc/xml/vtoolrecord.h"      // Brings in VToolRecord, the history entries added to the stub document.
#include "../../libs/vmisc/def.h"                // Brings in the Unit and Tool enums used below.

#include <QtTest> // Provides QCOMPARE/QVERIFY and the QTest infrastructure this file's slots run under.

namespace
{
    // Minimal concrete VAbstractPattern: implements every pure virtual with a trivial, no-op
    // body since this test never exercises XML parsing, label generation, or reference
    // counting -- only getHistory(), which VAbstractPattern already implements concretely.
    class TestPatternDoc : public VAbstractPattern
    {
    public:
        explicit TestPatternDoc(QObject *parent = nullptr) // Forwards straight to the base constructor.
            : VAbstractPattern(parent) // Base constructor reads default line settings via qApp, hence main.cpp's app bootstrap.
        {
        }

        void CreateEmptyFile() override {} // Never called: this test builds state directly, not from an empty document.

        void IncrementReferens(quint32 id) const override { Q_UNUSED(id) } // Reference counting is irrelevant to a read-only dump.
        void DecrementReferens(quint32 id) const override { Q_UNUSED(id) } // Reference counting is irrelevant to a read-only dump.

        QStringList GetCurrentAlphabet() const override { return QStringList(); } // No label alphabet needed for this test.

        QString GenerateLabel(const LabelType &type, const QString &reservedName = QString()) const override
        {
            Q_UNUSED(type)         // Label generation is outside pattern.dump's scope.
            Q_UNUSED(reservedName) // Label generation is outside pattern.dump's scope.
            return QString();      // No label text needed for this test.
        }

        QString GenerateSuffix(const QString &type) const override
        {
            Q_UNUSED(type)    // Suffix generation is outside pattern.dump's scope.
            return QString(); // No suffix text needed for this test.
        }

        void UpdateToolData(const quint32 &id, VContainer *data) override
        {
            Q_UNUSED(id)   // This test never re-parses tool data; history is populated directly instead.
            Q_UNUSED(data) // This test never re-parses tool data; history is populated directly instead.
        }

    public slots:
        void LiteParseTree(const Document &parse) override { Q_UNUSED(parse) } // Never invoked: no XML parsing occurs in this test.
    };
}

// Trivial constructor; every test slot builds its own container/document/context from scratch.
TST_PatternDump::TST_PatternDump(QObject *parent)
    : QObject(parent) // Forwards straight to QObject's constructor.
{
}

// Verifies "pattern.dump" reports exactly the points and history entries this test adds,
// mirroring what a real pattern's data container and document would expose.
void TST_PatternDump::testPatternDump()
{
    const Unit unit = Unit::Cm; // Working unit for the hand-built container; value is arbitrary but must outlive the container.
    VContainer data(nullptr, &unit); // No translator needed; this test never formats a formula.

    data.UpdateGObject(100, new VPointF(10.5, 20.25, QStringLiteral("A1"), 0.0, 0.0)); // First known point: id 100, name "A1".
    data.UpdateGObject(101, new VPointF(30.0, 5.0, QStringLiteral("A2"), 0.0, 0.0));   // Second known point: id 101, name "A2".

    TestPatternDoc doc; // Minimal document stub; only its history is populated below.
    doc.getHistory()->append(VToolRecord(2001, Tool::BasePoint, QStringLiteral("Draft1"))); // First history entry, in application order.
    doc.getHistory()->append(VToolRecord(2002, Tool::EndLine, QStringLiteral("Draft1")));   // Second history entry, in application order.

    ActionContext ctx(nullptr, &doc, &data); // No scene needed: pattern.dump never touches ctx.scene().
    ActionRegistry registry;                 // Auto-registers pattern.dump/listMeasurements/listTools on construction.
    ActionEngine engine(registry);           // Dispatches through the registry above.

    const QJsonObject action{{"op", QStringLiteral("pattern.dump")}};            // One action entry selecting the dump op.
    const QJsonDocument script(QJsonObject{{"actions", QJsonArray{action}}});    // Wraps it in the engine's expected script shape.

    const QJsonDocument output = engine.run(script, ctx); // Runs the script; should dispatch to handlePatternDump().
    const QJsonArray results = output.object().value("results").toArray(); // One result per input action.
    QCOMPARE(results.size(), 1); // Exactly one action was submitted, so exactly one result is expected back.

    const QJsonObject result = results.first().toObject(); // The (only) result entry for the dump action.
    QVERIFY2(result.value("ok").toBool(), "pattern.dump must succeed against a valid context"); // Guards the asserts below.

    const QJsonObject payload = result.value("value").toObject();     // The handler's actual {"objects","history"} payload.
    const QJsonArray objects = payload.value("objects").toArray();    // Every geometry object in the container.
    const QJsonArray history = payload.value("history").toArray();    // Every tool-history entry in the document.

    QCOMPARE(objects.size(), 2); // Exactly the two points added above.
    QCOMPARE(history.size(), 2); // Exactly the two history entries added above.

    bool foundA1 = false; // Tracks whether the known point "A1" was found; DataGObjects() is a QHash, so order is not guaranteed.
    for (const QJsonValue &value : objects) // Search the (unordered) objects array for the known point.
    {
        const QJsonObject entry = value.toObject(); // One object's JSON record.
        if (entry.value("name").toString() == QStringLiteral("A1")) // Found the point this test knows the exact coordinates of.
        {
            foundA1 = true; // Record that the spot-check target was located.
            QCOMPARE(entry.value("id").toInt(), 100); // Confirms the id round-tripped through the JSON payload.
            QCOMPARE(entry.value("type").toString(), QStringLiteral("Point")); // Confirms the GOType label is correct.
            QCOMPARE(entry.value("x").toDouble(), 10.5);  // Confirms the x coordinate round-tripped exactly.
            QCOMPARE(entry.value("y").toDouble(), 20.25); // Confirms the y coordinate round-tripped exactly.
            break; // No need to keep searching once the target entry is found.
        }
    }
    QVERIFY2(foundA1, "expected point \"A1\" (id 100) in the objects array"); // Fails clearly if the point was never found.

    // History is a QVector, so insertion order is preserved and safe to assert on directly.
    QCOMPARE(history.at(0).toObject().value("tool").toString(), QStringLiteral("BasePoint")); // First entry's tool name.
    QCOMPARE(history.at(0).toObject().value("draftBlock").toString(), QStringLiteral("Draft1")); // First entry's draft block.
    QCOMPARE(history.at(1).toObject().value("tool").toString(), QStringLiteral("EndLine"));   // Second entry's tool name.
}

// Verifies "pattern.listMeasurements" succeeds with an empty array on a container with no
// measurements attached -- an empty result must not be reported as an error.
void TST_PatternDump::testListMeasurementsEmpty()
{
    const Unit unit = Unit::Cm; // Working unit for the hand-built container; no measurements are added below.
    VContainer data(nullptr, &unit); // Deliberately left without any measurement variables.

    TestPatternDoc doc; // Document is unused by this handler, but ActionContext still requires one.

    ActionContext ctx(nullptr, &doc, &data); // No scene needed: pattern.listMeasurements never touches ctx.scene().
    ActionRegistry registry;                 // Auto-registers pattern.dump/listMeasurements/listTools on construction.
    ActionEngine engine(registry);           // Dispatches through the registry above.

    const QJsonObject action{{"op", QStringLiteral("pattern.listMeasurements")}};  // One action entry selecting the op.
    const QJsonDocument script(QJsonObject{{"actions", QJsonArray{action}}});      // Wraps it in the engine's expected script shape.

    const QJsonDocument output = engine.run(script, ctx); // Runs the script; should dispatch to handleListMeasurements().
    const QJsonArray results = output.object().value("results").toArray(); // One result per input action.
    QCOMPARE(results.size(), 1); // Exactly one action was submitted, so exactly one result is expected back.

    const QJsonObject result = results.first().toObject(); // The (only) result entry for this action.
    QVERIFY2(result.value("ok").toBool(), "pattern.listMeasurements must succeed even with no measurements attached");

    const QJsonArray measurements = result.value("value").toObject().value("measurements").toArray(); // The (empty) payload array.
    QCOMPARE(measurements.size(), 0); // An empty array is the correct, non-error result here.
}

// Verifies "pattern.listTools" reports exactly the three op names Phase 1 registers.
void TST_PatternDump::testListTools()
{
    const Unit unit = Unit::Cm; // Working unit for the hand-built container; unused by this handler.
    VContainer data(nullptr, &unit); // Unused by pattern.listTools, but ActionContext still requires one.
    TestPatternDoc doc;               // Unused by pattern.listTools, but ActionContext still requires one.

    ActionContext ctx(nullptr, &doc, &data); // No scene needed: pattern.listTools never touches ctx.scene().
    ActionRegistry registry;                 // Auto-registers pattern.dump/listMeasurements/listTools on construction.
    ActionEngine engine(registry);           // Dispatches through the registry above.

    const QJsonObject action{{"op", QStringLiteral("pattern.listTools")}};       // One action entry selecting the op.
    const QJsonDocument script(QJsonObject{{"actions", QJsonArray{action}}});    // Wraps it in the engine's expected script shape.

    const QJsonDocument output = engine.run(script, ctx); // Runs the script; should dispatch to handleListTools().
    const QJsonArray results = output.object().value("results").toArray(); // One result per input action.
    QCOMPARE(results.size(), 1); // Exactly one action was submitted, so exactly one result is expected back.

    const QJsonObject result = results.first().toObject(); // The (only) result entry for this action.
    QVERIFY2(result.value("ok").toBool(), "pattern.listTools must always succeed; it reports static data only");

    const QJsonArray tools = result.value("value").toObject().value("tools").toArray(); // The reported op-name list.

    QStringList toolNames; // Flattened list of reported names, for order-independent membership checks.
    for (const QJsonValue &value : tools) // Walk the reported array...
    {
        toolNames.append(value.toString()); // ...collecting each op name as a plain string.
    }

    QVERIFY2(toolNames.contains(QStringLiteral("pattern.dump")), "pattern.dump must be listed");                     // Phase 1 op #1.
    QVERIFY2(toolNames.contains(QStringLiteral("pattern.listMeasurements")), "pattern.listMeasurements must be listed"); // Phase 1 op #2.
    QVERIFY2(toolNames.contains(QStringLiteral("pattern.listTools")), "pattern.listTools must be listed");             // Phase 1 op #3.
}
