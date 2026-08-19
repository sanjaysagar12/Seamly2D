//---------------------------------------------------------------------------------------------------------------------
//  @file   name_resolver.cpp
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

#include "name_resolver.h" // Brings in the NameResolver class declaration this file implements.

// TODO: Phase 2 -- Name resolution layer. Until then this always returns 0 (an id no pattern
// object can have) and does not consult the container at all.
quint32 NameResolver::idForName(const QString &name, const VContainer *data)
{
    Q_UNUSED(name) // Unused until Phase 2 implements real name-to-id lookup.
    Q_UNUSED(data) // Unused until Phase 2 implements real name-to-id lookup.

    return 0; // Stub result: 0 signals "not resolved" until Phase 2 lands.
}

// TODO: Phase 2 -- Name resolution layer. Until then this always returns an empty string
// and does not consult the container at all.
QString NameResolver::nameForId(quint32 id, const VContainer *data)
{
    Q_UNUSED(id)   // Unused until Phase 2 implements real id-to-name lookup.
    Q_UNUSED(data) // Unused until Phase 2 implements real id-to-name lookup.

    return QString(); // Stub result: empty string until Phase 2 lands.
}
