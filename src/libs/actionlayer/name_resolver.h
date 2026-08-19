//---------------------------------------------------------------------------------------------------------------------
//  @file   name_resolver.h
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

#ifndef NAME_RESOLVER_H // Include guard start, prevents this header being processed twice in one translation unit.
#define NAME_RESOLVER_H // Marks NAME_RESOLVER_H as defined for the remainder of the include guard.

#include <QString>  // Provides QString, used by reference/value in both method signatures below.
#include <QtGlobal> // Provides quint32, the id type used by both resolver methods below.

class VContainer; // Forward declaration; only a pointer to it appears in the signatures below.

// NameResolver will translate between human-readable pattern object names and their internal ids.
class NameResolver
{
public:
    // Looks up the internal id for a named pattern object. Stubbed in Phase 0; see the .cpp file.
    static quint32 idForName(const QString &name, const VContainer *data); // Implemented in name_resolver.cpp.

    // Looks up the human-readable name for an internal pattern object id. Stubbed in Phase 0; see the .cpp file.
    static QString nameForId(quint32 id, const VContainer *data); // Implemented in name_resolver.cpp.
};

#endif // NAME_RESOLVER_H // End of include guard started above.
