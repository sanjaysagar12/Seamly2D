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

#include <QString>     // Provides QString, used by reference/value throughout this header.
#include <QStringList> // Provides QStringList, the type of ActionResolverError's knownNames list.
#include <QtGlobal>    // Provides quint32, the id type used by both resolver methods below.
#include <stdexcept>   // Provides std::runtime_error, ActionResolverError's base class.

// resolveTyped() below is a template method that calls VContainer::GeometricObject<T>(), itself a
// template member function -- the compiler needs VContainer's full definition at every call site
// this header is included from, not just a forward declaration.
#include "../vpatterndb/vcontainer.h"

// ActionResolverError is thrown by NameResolver when a JSON action names a pattern object that
// does not exist. It carries both the offending name and a fresh snapshot of every name that
// *does* exist, so ActionEngine can serialize a self-correcting error an automated (AI) caller can
// act on directly, instead of just a message string.
class ActionResolverError : public std::runtime_error
{
public:
    // Builds the error from the name that failed to resolve and the known-good names at throw time.
    ActionResolverError(const QString &missingName, const QStringList &knownNames)
        : std::runtime_error(("Unknown object name: " + missingName).toStdString()), // std::runtime_error requires a message at construction; this doubles as what().
          m_name(missingName),    // Store the offending name for name() below.
          m_knownNames(knownNames) // Store the known-good names for knownNames() below.
    {
    }

    // Getter returning the name that failed to resolve.
    QString name() const { return m_name; } // Returns the stored missing name unchanged.

    // Getter returning every name known to the container at throw time (may be empty; see NameResolver::nameForId).
    QStringList knownNames() const { return m_knownNames; } // Returns the stored known-names snapshot unchanged.

private:
    QString m_name;          // The name (or, for nameForId(), the numeric id as text) that failed to resolve.
    QStringList m_knownNames; // Every object name found in the container at throw time; empty when not meaningful.
};

// NameResolver translates between human-readable pattern object names and their internal ids.
// Every lookup is a linear scan over VContainer::DataGObjects() rather than a cached reverse map:
// DataGObjects() returns a live pointer into mutable state, and a cache would go stale the moment
// any later action (a future mutating handler) creates or renames an object. This is deliberately
// deferred optimization, not an oversight -- revisit only if profiling shows it matters.
class NameResolver
{
public:
    // Looks up the internal id for a named pattern object. Throws ActionResolverError if no
    // object in data->DataGObjects() has this name. Implemented in name_resolver.cpp.
    static quint32 idForName(const QString &name, const VContainer *data);

    // Looks up the human-readable name for an internal pattern object id. Throws
    // ActionResolverError if the id is not present in data->DataGObjects(). Implemented in
    // name_resolver.cpp.
    static QString nameForId(quint32 id, const VContainer *data);

    // Convenience typed wrapper: resolves name to an id, then returns it as a QSharedPointer<T>
    // via VContainer::GeometricObject<T>(). Throws ActionResolverError (unresolved name) or
    // VExceptionBadId (id resolved but T is the wrong type for it) exactly as those two calls do
    // individually.
    template <class T>
    static QSharedPointer<T> resolveTyped(const QString &name, const VContainer *data)
    {
        const quint32 id = idForName(name, data); // Throws ActionResolverError if unresolved; propagates unchanged.
        return data->GeometricObject<T>(id);      // Typed accessor; id was just confirmed present in data.
    }
};

#endif // NAME_RESOLVER_H // End of include guard started above.
