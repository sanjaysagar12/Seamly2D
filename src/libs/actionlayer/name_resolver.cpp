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

#include "../vgeometry/vgobject.h" // Brings in VGObject::name(), read from every entry in the scan below.

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
}

// Linear scan over data->DataGObjects() comparing each object's name() to the requested name.
// See the class comment in name_resolver.h for why this is deliberately not cached.
quint32 NameResolver::idForName(const QString &name, const VContainer *data)
{
    const QHash<quint32, QSharedPointer<VGObject>> *gObjects = data->DataGObjects(); // Every geometry object, keyed by id.

    bool found = false;   // Tracks whether a match has already been recorded, for the uniqueness assert below.
    quint32 foundId = 0;  // The id of the match, once found.

    if (gObjects != nullptr) // DataGObjects() can return nullptr before a pattern has been parsed.
    {
        for (auto it = gObjects->constBegin(); it != gObjects->constEnd(); ++it) // Walk every entry in the hash.
        {
            if (!it.value().isNull() && it.value()->name() == name) // Skip null entries; compare the rest by name.
            {
                // VContainer::uniqueNames (vcontainer.cpp) enforces name uniqueness across the whole
                // container for every object created through the normal Create()/AddGObject() path,
                // so finding a second match here means a bug in this scan or in that invariant, not a
                // real state production code needs to handle gracefully -- hence an assert, not a branch.
                Q_ASSERT_X(!found, "NameResolver::idForName", "duplicate object name found in DataGObjects()");
                found = true;      // Record that a match now exists, for the assert above on any later iteration.
                foundId = it.key(); // The hash key is the object's own id.
            }
        }
    }

    if (!found) // No object in the container carries this name.
    {
        throw ActionResolverError(name, collectKnownNames(data)); // Structured, self-correcting failure for an automated caller.
    }

    return foundId; // The (unique) id of the matching object.
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
