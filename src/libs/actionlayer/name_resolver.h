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

// Draw::Calculation/Modeling/Layout, used by the scoped idForName() overload below to tell a
// "real" calculation-section point apart from a same-named piece-node clone (VContainer can
// legitimately hold both once any piece exists -- see that overload's own comment).
#include "../vgeometry/vgeometrydef.h"

// ActionResolverError is thrown by NameResolver when a JSON action names a pattern object that
// does not exist, is ambiguous, or (for the scoped idForName() overload) exists only outside the
// requested Draw scope. It carries both the offending name and a fresh snapshot of every name
// that *does* exist, so ActionEngine can serialize a self-correcting error an automated (AI)
// caller can act on directly, instead of just a message string.
class ActionResolverError : public std::runtime_error
{
public:
    // What specifically went wrong; lets ActionEngine's catch clause (action_engine.cpp) build a
    // more specific structured error than "name not found" for the two cases that aren't that.
    enum class Kind
    {
        NotFound,  // No object in the container has this name, in any Draw mode.
        WrongScope, // The name exists, but only in a Draw mode other than the one the caller required.
        Duplicate  // More than one object in the required scope shares this name -- a real data
                   // integrity problem (VContainer::uniqueNames should prevent this within one
                   // scope), not something a caller can route around by supplying more context.
    };

    // Builds a NotFound error (the common case) from the name that failed to resolve and the
    // known-good names at throw time. wrongScopeFoundAs stays empty, matching Kind::NotFound.
    ActionResolverError(const QString &missingName, const QStringList &knownNames)
        : std::runtime_error(("Unknown object name: " + missingName).toStdString()), // std::runtime_error requires a message at construction; this doubles as what().
          m_kind(Kind::NotFound),
          m_name(missingName),    // Store the offending name for name() below.
          m_knownNames(knownNames) // Store the known-good names for knownNames() below.
    {
    }

    // Builds a WrongScope or Duplicate error. `kind` must be one of those two (NotFound has its
    // own two-argument constructor above); `wrongScopeFoundAs` names the Draw mode the name was
    // actually found in for Kind::WrongScope ("modeling", "layout", ...), and is empty/unused for
    // Kind::Duplicate.
    ActionResolverError(Kind kind, const QString &name, const QStringList &knownNames, const QString &wrongScopeFoundAs)
        : std::runtime_error(
              (kind == Kind::WrongScope
                   ? "Object name exists, but not in the required scope: " + name
                   : "Ambiguous object name (more than one match in the required scope): " + name)
                  .toStdString()),
          m_kind(kind),
          m_name(name),
          m_knownNames(knownNames),
          m_wrongScopeFoundAs(wrongScopeFoundAs)
    {
    }

    // Getter returning which failure mode this is; see the Kind enum above.
    Kind kind() const { return m_kind; }

    // Getter returning the name that failed to resolve.
    QString name() const { return m_name; } // Returns the stored missing name unchanged.

    // Getter returning every name known to the container at throw time (may be empty; see NameResolver::nameForId).
    QStringList knownNames() const { return m_knownNames; } // Returns the stored known-names snapshot unchanged.

    // Getter returning the Draw mode (as a lowercase string: "calculation"/"modeling"/"layout")
    // the name was actually found in, for Kind::WrongScope only; empty for every other Kind.
    QString wrongScopeFoundAs() const { return m_wrongScopeFoundAs; }

private:
    Kind       m_kind;             // Which of the three failure modes this error represents.
    QString    m_name;             // The name (or, for nameForId(), the numeric id as text) that failed to resolve.
    QStringList m_knownNames;      // Every object name found in the container at throw time; empty when not meaningful.
    QString    m_wrongScopeFoundAs; // Draw mode the name was found in, for Kind::WrongScope only; empty otherwise.
};

// NameResolver translates between human-readable pattern object names and their internal ids.
// Every lookup is a linear scan over VContainer::DataGObjects() rather than a cached reverse map:
// DataGObjects() returns a live pointer into mutable state, and a cache would go stale the moment
// any later action (a future mutating handler) creates or renames an object. This is deliberately
// deferred optimization, not an oversight -- revisit only if profiling shows it matters.
class NameResolver
{
public:
    // Looks up the internal id for a named pattern object, regardless of its Draw mode. Throws
    // ActionResolverError (Kind::NotFound if no match at all, Kind::Duplicate if more than one
    // object -- in any mode -- shares this name) if no unique id can be returned. Implemented in
    // name_resolver.cpp.
    //
    // PREFER THE SCOPED OVERLOAD BELOW for any name that names a calculation-context object (a
    // formula's basePoint/firstPoint/center/... argument, or any other reference meant to bind to
    // "the live object", not a frozen piece-node clone). This unscoped overload matches any Draw
    // mode, including Draw::Modeling -- once any piece.addPatternPiece/piece.internalPath has run,
    // VContainer legitimately contains a same-named Draw::Modeling clone alongside the original
    // Draw::Calculation object for every point that piece uses (VPattern::ParseNodePoint,
    // src/app/seamly2d/xml/vpattern.cpp, deliberately gives a reloaded piece-node clone the same
    // name() as the point it wraps -- core, intentional behavior, not itself a bug), and this
    // overload has no way to prefer one over the other. Kept for the few genuinely mode-agnostic
    // callers -- see e.g. handlePatternResolveName() in pattern_resolve_name_handler.cpp (the
    // "pattern.resolveName" diagnostic op, which explicitly wants "whatever this name resolves
    // to, in the container as a whole", including a Kind::Duplicate report for a genuinely
    // ambiguous name) and documents its own reasoning at its call site. Every calculation-context
    // handler (line_handlers.cpp, formula_point_handlers.cpp, curve_handlers.cpp,
    // cutpoint_handlers.cpp, operation_handlers.cpp, piece_handlers.cpp, and render_handlers.cpp's
    // "highlight" resolution) uses the scoped overload instead.
    static quint32 idForName(const QString &name, const VContainer *data);

    // Scoped variant of idForName(): only matches objects whose getMode() == requiredMode. Throws
    // ActionResolverError with:
    //   - Kind::NotFound if no object anywhere has this name at all (in any mode);
    //   - Kind::WrongScope if the name exists, but only outside requiredMode (wrongScopeFoundAs()
    //     names which mode it was actually found in) -- the case this overload exists to catch:
    //     see the unscoped overload's own comment for why a same-named Draw::Modeling clone can
    //     coexist with the Draw::Calculation object a calculation-context caller actually wants;
    //   - Kind::Duplicate if more than one object *within* requiredMode shares this name (a real
    //     VContainer::uniqueNames invariant violation within the requested scope, not a normal,
    //     expected situation the way a cross-scope name collision is).
    // Implemented in name_resolver.cpp.
    static quint32 idForName(const QString &name, const VContainer *data, Draw requiredMode);

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

    // Lowercases a Draw enumerator into the same string form ActionResolverError::
    // wrongScopeFoundAs() and action_engine.cpp's "foundInScope" JSON field use. Exposed publicly
    // (name_resolver.cpp's own scan logic uses an identical private copy) so every handler file's
    // defense-in-depth "is this id actually in the scope I required?" check (see e.g.
    // line_handlers.cpp's checkIsPoint()) can report the actual wrong mode without each file
    // duplicating this switch itself. Implemented in name_resolver.cpp.
    static QString drawModeToString(Draw mode);
};

#endif // NAME_RESOLVER_H // End of include guard started above.
