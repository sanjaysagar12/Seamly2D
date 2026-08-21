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
#include "action_schema.h" // Provides ActionSchema, the descriptive metadata every registerAction() call must now supply alongside its handler.

#include <QHash>       // Provides QHash, used to map action names to their handler functions/schemas.
#include <QString>     // Provides QString, used as the action name key type.
#include <QJsonObject> // Provides QJsonObject, used in the handler function's argument type.
#include <QVector>     // Provides QVector, the return type of allSchemas().
#include <functional>  // Provides std::function, used to store the handler callables.

class ActionContext; // Forward declaration; only a const reference to it appears in the handler signature.

// ActionRegistry holds the mapping from action op name to the function that implements it, and (as
// of 21 Aug, 2026) to a structured ActionSchema describing that op's parameters for `actiond --list-tools`
// (see tool_catalog.h). The default constructor registers every built-in handler (pattern.dump,
// basePoint, endLine, ...) so any freshly constructed registry is immediately usable by
// ActionEngine -- and by `actiond --list-tools`, which needs no pattern/scene at all -- without extra
// setup calls.
class ActionRegistry
{
public:
    // Alias for the handler signature every registered action function must match.
    using ActionFn = std::function<ActionResult(const QJsonObject&, const ActionContext&)>; // Keeps signatures below readable.

    // Constructs an empty map, then registers every built-in handler into it.
    ActionRegistry(); // Implemented in action_registry.cpp.

    // Registers a handler function under the given action name, overwriting any prior entry with
    // that name, together with the ActionSchema describing its parameters. The schema argument is
    // mandatory (not defaulted/overloaded away) specifically so a new op cannot be wired into this
    // registry without also supplying the descriptive metadata `actiond --list-tools --format=human`/`--format=ai` reports --
    // the single-source-of-truth property Step 1 of the tools-command work required, replacing
    // pattern_list_tools_handler.cpp's separately hand-maintained (and previously out-of-sync) op list.
    void registerAction(const QString &name, ActionFn fn, ActionSchema schema); // Implemented in action_registry.cpp.

    // Returns true if a handler has already been registered under the given action name.
    bool hasAction(const QString &name) const; // Implemented in action_registry.cpp.

    // Returns the handler registered under the given name, or an empty (falsy) std::function if none exists.
    ActionFn action(const QString &name) const; // Implemented in action_registry.cpp.

    // Returns how many handlers are actually registered (m_actions.size()), independent of
    // m_schemas -- exists so a regression test can assert allSchemas().size() equals this count
    // from a source that does not merely restate allSchemas()'s own size back at itself (see
    // src/test/ActionLayerTest/tst_action_schema.cpp).
    int actionCount() const; // Implemented in action_registry.cpp.

    // Returns the ActionSchema registered under the given name, or nullptr if none exists. The
    // returned pointer is only valid as long as this ActionRegistry instance is alive (it points
    // into m_schemas, not a copy).
    const ActionSchema *schema(const QString &name) const; // Implemented in action_registry.cpp.

    // Returns every registered op's ActionSchema, sorted alphabetically by op name for a stable,
    // deterministic listing order -- both `actiond --list-tools --format=human`/`--format=ai` (tool_catalog.cpp) and the
    // regression test asserting "one schema per registered action" (see
    // src/test/ActionLayerTest/tst_action_schema.cpp) rely on this covering every entry in
    // m_actions with none missing and none duplicated.
    QVector<ActionSchema> allSchemas() const; // Implemented in action_registry.cpp.

private:
    // Registers every built-in handler (pattern.dump, basePoint, endLine, ...) under its op name,
    // each paired with its ActionSchema. Called once, from the constructor. This is the one place
    // that lists every currently-registered op -- pattern_list_tools_handler.cpp's own hand-written
    // list and docs/action-layer-schema.md are both meant to be cross-checked against this file (and
    // now against the live `actiond --list-tools` output this file's schemas feed), not the other way round.
    void registerBuiltinActions(); // Implemented in action_registry.cpp.

    QHash<QString, ActionFn> m_actions;     // Maps op name -> handler; populated by registerBuiltinActions() at construction.
    QHash<QString, ActionSchema> m_schemas; // Maps op name -> descriptive schema; populated in lockstep with m_actions by every registerAction() call.
};

#endif // ACTION_REGISTRY_H // End of include guard started above.
