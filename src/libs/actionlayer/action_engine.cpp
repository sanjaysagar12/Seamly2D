//---------------------------------------------------------------------------------------------------------------------
//  @file   action_engine.cpp
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

#include "action_engine.h" // Brings in the ActionEngine class declaration this file implements.

#include "action_context.h" // Brings in the ActionContext type used in run()'s parameter list.
#include <QJsonObject>       // Provides QJsonObject, needed to read the script's top-level object.

// Constructor stores the registry reference for later use once dispatch is implemented.
ActionEngine::ActionEngine(ActionRegistry &registry)
    : m_registry(registry) // Initialize the reference member from the constructor argument.
{
}

// Parses the "actions" array out of the script's top-level JSON object. This is intentionally a
// no-op in Phase 0: dispatching parsed entries through the ActionRegistry is deferred to a later
// phase, so this only proves the script shape can be read and always returns an empty result.
QJsonArray ActionEngine::run(const QJsonDocument &script, ActionContext &ctx)
{
    Q_UNUSED(m_registry) // Registry is not consulted yet; dispatch is added in a later phase.
    Q_UNUSED(ctx)         // Context is not used yet; it will be passed to dispatched handlers later.

    const QJsonObject root = script.object();               // Read the top-level JSON object from the script document.
    const QJsonArray actions = root.value("actions").toArray(); // Read the "actions" array; empty if absent or wrong type.
    Q_UNUSED(actions) // Parsed but intentionally not iterated in Phase 0; dispatch comes later.

    return QJsonArray(); // Phase 0 no-op: always return an empty array, nothing has been dispatched.
}
