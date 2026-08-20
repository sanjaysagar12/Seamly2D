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

#include "name_resolver.h" // Brings in NameResolver and ActionResolverError, both implemented in this file.

#include "../vgeometry/vgobject.h" // Brings in VGObject::name()/getMode(), read from every entry in the scan below.

namespace
{
    // Snapshots every object name currently in the container, for ActionResolverError's
    // knownNames() -- populated fresh at throw time rather than maintained separately, so it is
    // always accurate even though that costs an extra scan on the (rare) failure path.
    QStringList collectKnownNames(const VContainer *data)
    {
        QStringList names; // Accumulates one entry per non-null geometry object.

        const QHash<quint32, QSharedPointer<VGObject>> *gObjects = data->DataGObjects(); // Every geometry object, keyed by id.
        if (gObjects != nullptr) // DataGObjects() can return nullptr before a pattern has been parsed.
        {
            for (auto it = gObjects->constBegin(); it != gObjects->constEnd(); ++it) // Walk every entry in the hash.
            {
                if (!it.value().isNull()) // Defensive guard: skip a null entry instead of dereferencing it.
                {
                    names.append(it.value()->name()); // Record this object's current name.
                }
            }
        }

        return names; // Return the collected snapshot by value.
    }

    // Shared scan core for both idForName() overloads below. `requiredMode` is a pointer so the
    // unscoped overload can pass nullptr ("match any mode") without duplicating the whole loop;
    // the scoped overload passes the address of its own by-value Draw argument.
    //
    // On a within-scope duplicate, throws immediately (Kind::Duplicate) rather than returning --
    // this replaces the old Q_ASSERT_X, which was compiled to nothing in this project's release
    // build (V_NO_ASSERT/NDEBUG; see actionlayer.pro) and so never actually caught anything outside
    // a debug build. VContainer::uniqueNames (vcontainer.cpp) enforces name uniqueness *within* a
    // single Draw mode for every object created through the normal Create()/AddGObject() path, so a
    // same-mode duplicate here is a genuine data-integrity problem, not a normal, expected
    // situation the way a *cross*-mode collision (a calculation point vs. its own piece-node
    // clone) now is.
    quint32 scanForName(const QString &name, const VContainer *data, const Draw *requiredMode)
    {
        const QHash<quint32, QSharedPointer<VGObject>> *gObjects = data->DataGObjects(); // Every geometry object, keyed by id.

        bool found = false;          // A same-scope match has been recorded.
        quint32 foundId = 0;         // Its id, once found.
        bool foundWrongScope = false; // A match exists, but only outside requiredMode (scoped overload only).
        Draw wrongScopeMode = Draw::Calculation; // Meaningless unless foundWrongScope is true.

        if (gObjects != nullptr) // DataGObjects() can return nullptr before a pattern has been parsed.
        {
            for (auto it = gObjects->constBegin(); it != gObjects->constEnd(); ++it) // Walk every entry in the hash.
            {
                if (it.value().isNull() || it.value()->name() != name) // Skip null entries and non-matching names.
                {
                    continue;
                }

                if (requiredMode == nullptr || it.value()->getMode() == *requiredMode) // In scope (or scope-agnostic).
                {
                    if (found) // A second in-scope match: genuine ambiguity, not routable around by the caller.
                    {
                        throw ActionResolverError(ActionResolverError::Kind::Duplicate, name, collectKnownNames(data), QString());
                    }
                    found = true;
                    foundId = it.key(); // The hash key is the object's own id.
                }
                else if (!foundWrongScope) // First out-of-scope match; remembered only for the WrongScope error path below.
                {
                    foundWrongScope = true;
                    wrongScopeMode = it.value()->getMode();
                }
            }
        }

        if (!found) // No in-scope object carries this name.
        {
            if (requiredMode != nullptr && foundWrongScope) // Scoped lookup, and the name exists -- just not here.
            {
                throw ActionResolverError(ActionResolverError::Kind::WrongScope, name, collectKnownNames(data),
                    NameResolver::drawModeToString(wrongScopeMode));
            }
            throw ActionResolverError(name, collectKnownNames(data)); // Kind::NotFound: no match in any mode.
        }

        return foundId; // The (unique, in-scope) id of the matching object.
    }
}

// See name_resolver.h for the full contract; matches any Draw mode.
quint32 NameResolver::idForName(const QString &name, const VContainer *data)
{
    return scanForName(name, data, nullptr);
}

// See name_resolver.h for the full contract; matches only objects in requiredMode.
quint32 NameResolver::idForName(const QString &name, const VContainer *data, Draw requiredMode)
{
    return scanForName(name, data, &requiredMode);
}

// Linear scan over data->DataGObjects() for the given id; DataGObjects() is already keyed by id,
// so this is a direct hash lookup rather than a scan, but still consults the live container each
// call for the same staleness reason idForName() does.
QString NameResolver::nameForId(quint32 id, const VContainer *data)
{
    const QHash<quint32, QSharedPointer<VGObject>> *gObjects = data->DataGObjects(); // Every geometry object, keyed by id.

    if (gObjects != nullptr && gObjects->contains(id) && !gObjects->value(id).isNull()) // The id must exist and map to a real object.
    {
        return gObjects->value(id)->name(); // Found: report its current name.
    }

    // Reuses ActionResolverError for id-lookup failures too, encoding the missing id as text in the
    // "name" field so ActionEngine's single catch clause can serialize both failure modes the same
    // way. An id has no notion of "known good names" to suggest, so that list is left empty here.
    throw ActionResolverError(QString::number(id), QStringList());
}

// See name_resolver.h for the full contract.
QString NameResolver::drawModeToString(Draw mode)
{
    switch (mode)
    {
        case Draw::Calculation: return QStringLiteral("calculation");
        case Draw::Modeling:    return QStringLiteral("modeling");
        case Draw::Layout:      return QStringLiteral("layout");
    }
    return QStringLiteral("unknown"); // Defensive: every current Draw enumerator is handled above; never reached.
}
