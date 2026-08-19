//---------------------------------------------------------------------------------------------------------------------
//  @file   tst_name_resolver.h
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

#ifndef TST_NAME_RESOLVER_H // Include guard start, prevents this header being processed twice in one translation unit.
#define TST_NAME_RESOLVER_H // Marks TST_NAME_RESOLVER_H as defined for the remainder of the include guard.

#include <QObject> // Provides QObject, the base class QTest requires for a test class.

// Exercises NameResolver directly against a hand-built VContainer -- pure C++, no JSON/ActionEngine
// involved. tst_action_engine_batches.cpp covers the JSON-facing "pattern.resolveName" op and
// ActionEngine's structured-error serialization on top of this.
class TST_NameResolver : public QObject
{
    Q_OBJECT // Enables QTest's slot discovery and signal/slot support for this class.
public:
    explicit TST_NameResolver(QObject *parent = nullptr); // Trivial constructor; all state is built per-test below.

private slots:
    void testResolveExistingName();     // idForName() returns the correct id for a name that exists.
    void testResolveMissingNameThrows(); // idForName() throws ActionResolverError, with the offending name and known names attached.
    void testReverseLookup();            // nameForId() round-trips correctly against idForName().
    void testResolveTypedTemplate();     // resolveTyped<VPointF>() returns a valid, correctly-typed shared pointer.
};

#endif // TST_NAME_RESOLVER_H // End of include guard started above.
