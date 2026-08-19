//---------------------------------------------------------------------------------------------------------------------
//  @file   action_registry.h
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

#ifndef ACTION_REGISTRY_H // Include guard start, prevents this header being processed twice in one translation unit.
#define ACTION_REGISTRY_H // Marks ACTION_REGISTRY_H as defined for the remainder of the include guard.

#include "action_result.h" // Provides ActionResult, the return type every registered handler must produce.

#include <QHash>       // Provides QHash, used to map action names to their handler functions.
#include <QString>     // Provides QString, used as the action name key type.
#include <QJsonObject> // Provides QJsonObject, used in the handler function's argument type.
#include <functional>  // Provides std::function, used to store the handler callables.

class ActionContext; // Forward declaration; only a const reference to it appears in the handler signature.

// ActionRegistry holds the mapping from action op name to the function that implements it.
// The default constructor registers Phase 1's built-in read-only handlers (pattern.dump,
// pattern.listMeasurements, pattern.listTools) so any freshly constructed registry is
// immediately usable by ActionEngine without extra setup calls.
class ActionRegistry
{
public:
    // Alias for the handler signature every registered action function must match.
    using ActionFn = std::function<ActionResult(const QJsonObject&, const ActionContext&)>; // Keeps signatures below readable.

    // Constructs an empty map, then registers Phase 1's built-in handlers into it.
    ActionRegistry(); // Implemented in action_registry.cpp.

    // Registers a handler function under the given action name, overwriting any prior entry with that name.
    void registerAction(const QString &name, ActionFn fn); // Implemented in action_registry.cpp.

    // Returns true if a handler has already been registered under the given action name.
    bool hasAction(const QString &name) const; // Implemented in action_registry.cpp.

    // Returns the handler registered under the given name, or an empty (falsy) std::function if none exists.
    ActionFn action(const QString &name) const; // Implemented in action_registry.cpp.

private:
    // Registers the three Phase 1 handlers (pattern.dump, pattern.listMeasurements,
    // pattern.listTools) under their op names. Called once, from the constructor.
    void registerBuiltinActions(); // Implemented in action_registry.cpp.

    QHash<QString, ActionFn> m_actions; // Maps op name -> handler; populated by registerBuiltinActions() at construction.
};

#endif // ACTION_REGISTRY_H // End of include guard started above.
