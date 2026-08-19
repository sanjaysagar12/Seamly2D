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

#include "action_context.h"  // Brings in ActionContext, passed through to each dispatched handler.
#include "action_registry.h" // Brings in ActionRegistry::action(), used to look up each op's handler.
#include "action_result.h"   // Brings in ActionResult, the per-action value this file serializes to JSON.

#include <QJsonArray>  // Provides QJsonArray, used for both the "actions" input and "results" output arrays.
#include <QJsonObject> // Provides QJsonObject, used for the script's top-level object and each action/result entry.

namespace
{
    // Converts one ActionResult into its JSON representation: {"op", "ok", "value", "error"}.
    // "op" records which action this result belongs to, since a script can run several in order.
    QJsonObject toJson(const ActionResult &result, const QString &op)
    {
        QJsonObject entry;         // Builds this result's JSON record.
        entry["op"] = op;          // Which op produced this result, for traceability in multi-action scripts.
        entry["ok"] = result.ok;   // Whether the action succeeded.
        entry["value"] = result.value; // The action's payload on success; null/default on failure.
        entry["error"] = result.error; // The failure reason on failure; empty string on success.
        return entry;               // Return the populated record by value.
    }
}

// Constructor stores the registry reference used to dispatch every parsed action.
ActionEngine::ActionEngine(ActionRegistry &registry)
    : m_registry(registry) // Initialize the reference member from the constructor argument.
{
}

// Parses the "actions" array out of the script's top-level JSON object, dispatches each entry
// through the registry by its "op" name, and collects every ActionResult into {"results": [...]}.
// An entry naming an unregistered op produces a failed result instead of being skipped or
// crashing, so callers always get one result per input action.
QJsonDocument ActionEngine::run(const QJsonDocument &script, const ActionContext &ctx)
{
    const QJsonObject root = script.object();                   // Read the top-level JSON object from the script document.
    const QJsonArray actions = root.value("actions").toArray(); // Read the "actions" array; empty if absent or wrong type.

    QJsonArray results; // Accumulates one JSON result record per input action, in order.

    for (const QJsonValue &actionValue : actions) // Walk every entry in the "actions" array, in script order.
    {
        const QJsonObject actionObject = actionValue.toObject(); // Each action is itself a JSON object; empty if malformed.
        const QString op = actionObject.value("op").toString();  // The op name selecting which handler runs.

        const ActionRegistry::ActionFn handler = m_registry.action(op); // Look up the handler registered under this op name.
        if (!handler) // An empty std::function means no handler is registered under this name.
        {
            const ActionResult failure = ActionResult::failure(QStringLiteral("Unknown action op: '%1'").arg(op)); // Clear, actionable error message.
            results.append(toJson(failure, op)); // Report the failure as this action's result instead of skipping it.
            continue; // Move on to the next action; one bad op must not abort the whole script.
        }

        const ActionResult result = handler(actionObject, ctx); // Dispatch to the registered handler with this action's fields.
        results.append(toJson(result, op)); // Record the handler's outcome, success or failure, in script order.
    }

    QJsonObject output; // Wraps the collected results under the documented output key.
    output["results"] = results; // "results": one entry per input action, in the same order.

    return QJsonDocument(output); // Wrap the output object as the QJsonDocument callers expect.
}
