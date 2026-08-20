//---------------------------------------------------------------------------------------------------------------------
//  @file   tst_name_resolver.cpp
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

#include "tst_name_resolver.h" // Brings in the TST_NameResolver class declaration this file implements.

#include "../../libs/actionlayer/name_resolver.h" // Brings in NameResolver and ActionResolverError, the classes under test.

#include "../../libs/vpatterndb/vcontainer.h" // Brings in VContainer, holding the hand-built points for these tests.
#include "../../libs/vgeometry/vpointf.h"     // Brings in VPointF, the point objects added to the container below.
#include "../../libs/vmisc/def.h"             // Brings in the Unit enum used below.

#include <QtTest> // Provides QCOMPARE/QVERIFY and the QTest infrastructure this file's slots run under.

// Trivial constructor; every test slot builds its own container from scratch.
TST_NameResolver::TST_NameResolver(QObject *parent)
    : QObject(parent) // Forwards straight to QObject's constructor.
{
}

// Verifies idForName() finds the correct id for a name that is actually present.
void TST_NameResolver::testResolveExistingName()
{
    const Unit unit = Unit::Cm; // Working unit for the hand-built container; value is arbitrary but must outlive the container.
    VContainer data(nullptr, &unit); // No translator needed; this test never formats a formula.

    data.UpdateGObject(100, new VPointF(10.5, 20.25, QStringLiteral("A1"), 0.0, 0.0)); // First known point: id 100, name "A1".
    data.UpdateGObject(101, new VPointF(30.0, 5.0, QStringLiteral("A2"), 0.0, 0.0));   // Second known point: id 101, name "A2".

    QCOMPARE(NameResolver::idForName(QStringLiteral("A1"), &data), quint32(100)); // "A1" must resolve to its actual id.
    QCOMPARE(NameResolver::idForName(QStringLiteral("A2"), &data), quint32(101)); // "A2" must resolve to its actual id.
}

// Verifies idForName() throws ActionResolverError for a name that is not present, and that the
// exception carries both the offending name and the actual set of known-good names.
void TST_NameResolver::testResolveMissingNameThrows()
{
    const Unit unit = Unit::Cm; // Working unit for the hand-built container.
    VContainer data(nullptr, &unit); // No translator needed; this test never formats a formula.

    data.UpdateGObject(100, new VPointF(10.5, 20.25, QStringLiteral("A1"), 0.0, 0.0)); // Only known point: id 100, name "A1".
    data.UpdateGObject(101, new VPointF(30.0, 5.0, QStringLiteral("A2"), 0.0, 0.0));   // Second known point: id 101, name "A2".

    bool threw = false; // Tracks whether the expected exception type was actually thrown.
    try
    {
        NameResolver::idForName(QStringLiteral("DoesNotExist"), &data); // Must throw: no object has this name.
    }
    catch (const ActionResolverError &error)
    {
        threw = true; // Confirms the correct exception type was thrown, not some other std::exception.
        QCOMPARE(error.name(), QStringLiteral("DoesNotExist")); // The exception must report the exact name that failed.
        QVERIFY2(error.knownNames().contains(QStringLiteral("A1")), "knownNames must contain the actually-present name \"A1\"");
        QVERIFY2(error.knownNames().contains(QStringLiteral("A2")), "knownNames must contain the actually-present name \"A2\"");
        QCOMPARE(error.knownNames().size(), 2); // Exactly the two names added above -- no phantom entries.
    }
    QVERIFY2(threw, "idForName() must throw ActionResolverError for an unknown name");
}

// Verifies nameForId() round-trips correctly against idForName() for a real object.
void TST_NameResolver::testReverseLookup()
{
    const Unit unit = Unit::Cm; // Working unit for the hand-built container.
    VContainer data(nullptr, &unit); // No translator needed; this test never formats a formula.

    data.UpdateGObject(100, new VPointF(10.5, 20.25, QStringLiteral("A1"), 0.0, 0.0)); // Known point: id 100, name "A1".

    const quint32 id = NameResolver::idForName(QStringLiteral("A1"), &data); // Forward direction: name -> id.
    QCOMPARE(NameResolver::nameForId(id, &data), QStringLiteral("A1"));      // Reverse direction: id -> name, must match the original.
}

// Verifies resolveTyped<VPointF>() returns a valid, correctly-typed, correctly-valued shared pointer.
void TST_NameResolver::testResolveTypedTemplate()
{
    const Unit unit = Unit::Cm; // Working unit for the hand-built container.
    VContainer data(nullptr, &unit); // No translator needed; this test never formats a formula.

    data.UpdateGObject(100, new VPointF(10.5, 20.25, QStringLiteral("A1"), 0.0, 0.0)); // Known point: id 100, name "A1".

    const QSharedPointer<VPointF> point = NameResolver::resolveTyped<VPointF>(QStringLiteral("A1"), &data); // Typed resolve.
    QVERIFY2(!point.isNull(), "resolveTyped<VPointF> must return a non-null pointer for a known point name");
    QCOMPARE(point->x(), 10.5);  // Confirms the returned object is the same point that was added.
    QCOMPARE(point->y(), 20.25); // Confirms the returned object is the same point that was added.
}

// Verifies the scoped idForName() overload's core fix: when a Draw::Calculation object and a
// Draw::Modeling object share a name -- exactly what VPattern::ParseNodePoint() (src/app/seamly2d/
// xml/vpattern.cpp) produces for every point a piece uses, by design, once that pattern is
// reloaded from disk -- each scope's own lookup must find its own object, not whichever one a
// QHash scan happens to hit.
void TST_NameResolver::testScopedLookupPrefersCalculationOverModelingClone()
{
    const Unit unit = Unit::Cm;
    VContainer data(nullptr, &unit);

    // id 100: the "real" calculation point, as basePoint/endLine/etc. would create.
    data.UpdateGObject(100, new VPointF(10.5, 20.25, QStringLiteral("A1"), 0.0, 0.0, 0, Draw::Calculation));
    // id 200: a piece-node clone of it, named identically -- mirrors VPattern::ParseNodePoint()'s
    // "new VPointF(*point, point->name(), mx, my, idObject, Draw::Modeling)" (idObject would be
    // 100 in the real flow; left as 0 here since idForName()'s scan reads only name()/getMode(),
    // never idObject, and this test's own hand-built VContainer has none of the full app/doc
    // machinery real idObject cross-references are resolved against).
    data.UpdateGObject(200, new VPointF(10.5, 20.25, QStringLiteral("A1"), 0.0, 0.0, 0, Draw::Modeling));

    const quint32 calcId = NameResolver::idForName(QStringLiteral("A1"), &data, Draw::Calculation);
    QCOMPARE(calcId, quint32(100));

    const quint32 modelId = NameResolver::idForName(QStringLiteral("A1"), &data, Draw::Modeling);
    QCOMPARE(modelId, quint32(200));

    // The unscoped overload matches "any Draw mode" by treating every object as in-scope (see
    // scanForName()'s `requiredMode == nullptr` branch in name_resolver.cpp) -- so two same-named
    // objects in *different* modes are, from its point of view, two in-scope matches for the same
    // name, which is exactly what Kind::Duplicate exists to report. This was originally written
    // expecting the unscoped overload to silently return whichever match a QHash scan hit first
    // (mirroring the pre-fix bug's own behavior) -- reproduced and corrected here once actually
    // running this test showed the real behavior is the stricter, more useful one: an ambiguous
    // name throws audibly instead of silently picking a winner, for pattern.resolveName's benefit
    // (see pattern_resolve_name_handler.cpp's own comment on why it deliberately uses this
    // overload and wants exactly this Kind::Duplicate signal for a cross-mode name collision).
    bool threw = false;
    try
    {
        NameResolver::idForName(QStringLiteral("A1"), &data);
    }
    catch (const ActionResolverError &error)
    {
        threw = true;
        QCOMPARE(error.kind(), ActionResolverError::Kind::Duplicate);
        QCOMPARE(error.name(), QStringLiteral("A1"));
    }
    QVERIFY2(threw, "the unscoped overload must throw ActionResolverError(Kind::Duplicate) for a name that exists in more than one Draw mode");
}

// Verifies that a Draw::Calculation-scoped lookup for a name that exists ONLY as a Draw::Modeling
// object throws Kind::WrongScope (not Kind::NotFound -- the name is known, just not in this
// scope) and correctly reports which mode it was actually found in.
void TST_NameResolver::testScopedLookupThrowsWrongScopeForModelingOnlyName()
{
    const Unit unit = Unit::Cm;
    VContainer data(nullptr, &unit);

    // Only a modeling-mode object named "NodeOnly" exists -- no calculation-mode counterpart.
    data.UpdateGObject(200, new VPointF(1.0, 2.0, QStringLiteral("NodeOnly"), 0.0, 0.0, 0, Draw::Modeling));

    bool threw = false;
    try
    {
        NameResolver::idForName(QStringLiteral("NodeOnly"), &data, Draw::Calculation);
    }
    catch (const ActionResolverError &error)
    {
        threw = true;
        QCOMPARE(error.kind(), ActionResolverError::Kind::WrongScope);
        QCOMPARE(error.name(), QStringLiteral("NodeOnly"));
        QCOMPARE(error.wrongScopeFoundAs(), QStringLiteral("modeling"));
    }
    QVERIFY2(threw, "a Draw::Calculation-scoped lookup for a modeling-only name must throw ActionResolverError(Kind::WrongScope)");
}

// Verifies that two objects sharing a name *within the same required scope* throw
// Kind::Duplicate -- the real data-integrity problem the old Q_ASSERT_X (compiled to nothing in
// this project's release build; see name_resolver.cpp) used to silently miss outside a debug
// build.
void TST_NameResolver::testScopedLookupThrowsDuplicateForSameScopeCollision()
{
    const Unit unit = Unit::Cm;
    VContainer data(nullptr, &unit);

    // Two distinct objects, both Draw::Calculation, both named "Dup" -- an invalid state
    // VContainer::uniqueNames() should prevent in real use, but the scan itself must still fail
    // safely (and audibly) if it is ever reached.
    data.UpdateGObject(100, new VPointF(0.0, 0.0, QStringLiteral("Dup"), 0.0, 0.0, 0, Draw::Calculation));
    data.UpdateGObject(101, new VPointF(1.0, 1.0, QStringLiteral("Dup"), 0.0, 0.0, 0, Draw::Calculation));

    bool threw = false;
    try
    {
        NameResolver::idForName(QStringLiteral("Dup"), &data, Draw::Calculation);
    }
    catch (const ActionResolverError &error)
    {
        threw = true;
        QCOMPARE(error.kind(), ActionResolverError::Kind::Duplicate);
        QCOMPARE(error.name(), QStringLiteral("Dup"));
    }
    QVERIFY2(threw, "a same-scope name collision must throw ActionResolverError(Kind::Duplicate)");
}
