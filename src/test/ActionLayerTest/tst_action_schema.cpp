//---------------------------------------------------------------------------------------------------------------------
//  @file   tst_action_schema.cpp
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

#include "tst_action_schema.h" // Brings in the TST_ActionSchema class declaration this file implements.

#include "../../libs/actionlayer/action_registry.h" // Brings in ActionRegistry, the data source under test.
#include "../../libs/actionlayer/action_schema.h"    // Brings in ActionSchema/ActionParamSchema, inspected below.

#include <QSet>
#include <QtTest>

// Trivial constructor; every test slot builds its own ActionRegistry from scratch.
TST_ActionSchema::TST_ActionSchema(QObject *parent)
    : QObject(parent) // Forwards straight to QObject's constructor.
{
}

// The core drift-detection assertion this whole file exists for: every registered action has
// exactly one schema (no gaps, no duplicates), and every schema actually corresponds to a
// registered handler.
void TST_ActionSchema::testEveryActionHasExactlyOneSchema()
{
    ActionRegistry registry; // Auto-registers every built-in handler + schema on construction.

    const QVector<ActionSchema> schemas = registry.allSchemas();

    // allSchemas() is read from a *separate* map (m_schemas) than actionCount() (m_actions) --
    // see action_registry.cpp's registerAction(), which inserts into both together. If a future
    // change ever let one map fall out of sync with the other, this is the check that catches it.
    QCOMPARE(schemas.size(), registry.actionCount());

    QSet<QString> seenOps;
    for (const ActionSchema &s : schemas)
    {
        QVERIFY2(!seenOps.contains(s.op), qPrintable(QStringLiteral("duplicate schema for op \"%1\"").arg(s.op)));
        seenOps.insert(s.op);

        QVERIFY2(registry.hasAction(s.op),
                 qPrintable(QStringLiteral("schema \"%1\" has no matching registered handler").arg(s.op)));
    }

    QCOMPARE(seenOps.size(), registry.actionCount()); // Restates the size check above in terms of distinct op names, guarding specifically against a duplicate silently masking a missing one.
}

// Sanity-checks the *content* of every schema, not just its presence: every op needs a real
// description and example, and (a common mistake this guards against) a field marked "required"
// should never also carry a "default" -- a required field has no default to fall back on by
// definition.
void TST_ActionSchema::testEverySchemaIsWellFormed()
{
    ActionRegistry registry;
    const QVector<ActionSchema> schemas = registry.allSchemas();
    QVERIFY(!schemas.isEmpty());

    static const QSet<QString> knownCategories = {
        QStringLiteral("introspection"), QStringLiteral("point"), QStringLiteral("formula-point"),
        QStringLiteral("curve"), QStringLiteral("cut-point"), QStringLiteral("operation"),
        QStringLiteral("piece"), QStringLiteral("measurements"), QStringLiteral("session"),
    };
    static const QSet<QString> knownJsonTypes = {
        QStringLiteral("string"), QStringLiteral("number"), QStringLiteral("boolean"),
        QStringLiteral("array"), QStringLiteral("object"),
    };

    for (const ActionSchema &s : schemas)
    {
        const QByteArray context = ("op \"" + s.op + "\"").toUtf8();

        QVERIFY2(!s.op.isEmpty(), "op must not be empty");
        QVERIFY2(!s.description.isEmpty(), context.constData());
        QVERIFY2(!s.exampleRequest.isEmpty(), context.constData());
        QVERIFY2(knownCategories.contains(s.category),
                 qPrintable(QStringLiteral("op \"%1\" has unknown category \"%2\"").arg(s.op, s.category)));

        if (s.partial) // partialReason is only meaningful (and required) when partial is set.
        {
            QVERIFY2(!s.partialReason.isEmpty(),
                     qPrintable(QStringLiteral("op \"%1\" is marked partial with no reason").arg(s.op)));
        }

        for (const ActionParamSchema &p : s.parameters)
        {
            const QByteArray paramContext = (s.op + "." + p.name).toUtf8();
            QVERIFY2(!p.name.isEmpty(), qPrintable(QStringLiteral("op \"%1\" has a parameter with an empty name").arg(s.op)));
            QVERIFY2(knownJsonTypes.contains(p.jsonType), paramContext.constData());
            QVERIFY2(!p.description.isEmpty(), paramContext.constData());
            QVERIFY2(!(p.required && !p.defaultValue.isEmpty()), paramContext.constData()); // A required field cannot also have a default.
            if (!p.itemsType.isEmpty())
            {
                QVERIFY2(p.jsonType == QStringLiteral("array"), paramContext.constData()); // itemsType is only meaningful for an array field.
            }
        }
    }
}
