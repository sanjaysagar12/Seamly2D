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
#include "action_registry.h" // Brings in ActionRegistry::action()/schema(), used to look up each op's handler and mutatesPattern flag.
#include "action_result.h"   // Brings in ActionResult, the per-action value this file serializes to JSON.
#include "action_schema.h"   // Brings in ActionSchema::mutatesPattern, read below to decide whether to open a macro.
#include "name_resolver.h"   // Brings in ActionResolverError, caught below so a bad name never escapes run() uncaught.

#include "../ifc/exception/vexception.h" // Brings in VException, caught below as a safety net (see the catch clause's own comment).

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

    // Phase 12: builds a short, diagnosable macro label for one JSON action -- e.g. `basePoint(A)`
    // or `piece.union(Front, Back)` -- rather than just the bare op name, so
    // "session.undoStatus"'s label window (see session_undo_handlers.cpp) lets a caller tell two
    // basePoint steps apart without re-running pattern.dump. Tries a short, fixed list of the
    // JSON field names actually used across this action layer's ~50 ops to carry a primary
    // identifying name (see action_registry.cpp's own parameter tables); falls back to the bare op
    // name when none of them is present or none is a non-empty string. Deliberately not a
    // per-op switch: a new op that happens to use one of these field names for its own primary
    // identifier gets a reasonable label for free, and one that doesn't just falls back cleanly.
    QString describeActionForMacroLabel(const QString &op, const QJsonObject &action)
    {
        static const QStringList identifyingFields = {
            QStringLiteral("name"), QStringLiteral("point"), QStringLiteral("piece"),
            QStringLiteral("curve"), QStringLiteral("arc")
        };
        for (const QString &field : identifyingFields)
        {
            const QJsonValue value = action.value(field);
            if (value.isString() && !value.toString().isEmpty())
            {
                return QStringLiteral("%1(%2)").arg(op, value.toString());
            }
        }
        // piece.union names two pieces, neither singly primary; no field above matches it.
        if (action.contains(QStringLiteral("piece1")) && action.contains(QStringLiteral("piece2")))
        {
            return QStringLiteral("%1(%2, %3)").arg(op, action.value(QStringLiteral("piece1")).toString(),
                                                      action.value(QStringLiteral("piece2")).toString());
        }
        return op; // No identifying field found (e.g. measurements.sync, group); the op name alone is still diagnosable.
    }

    // Phase 12: RAII guard guaranteeing endMutatingAction() runs exactly once for every
    // beginMutatingAction() call this loop makes, even if a handler throws something that escapes
    // every catch clause below (should never happen given the catch-all `...` clause, but this
    // guard makes that guarantee structural rather than relying on every future edit to this loop
    // preserving it). Mirrors the same "guarantee the matching close, no matter what" shape
    // QUndoStack::beginMacro()/endMacro() themselves need paired 1:1 -- see pattern_session.cpp's
    // own comment on what happens if that pairing is ever broken by a handler's own internal code.
    class MutatingActionGuard
    {
    public:
        MutatingActionGuard(const ActionEngine::EndMutatingActionFn &end, bool active)
            : m_end(end), m_active(active)
        {
        }
        ~MutatingActionGuard()
        {
            if (m_active && m_end)
            {
                m_end();
            }
        }
        Q_DISABLE_COPY(MutatingActionGuard)

    private:
        const ActionEngine::EndMutatingActionFn &m_end;
        bool m_active;
    };
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
QJsonDocument ActionEngine::run(const QJsonDocument &script, const ActionContext &ctx, bool abortOnFirstError,
                                 const BeginMutatingActionFn &beginMutatingAction,
                                 const EndMutatingActionFn &endMutatingAction)
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

        // Phase 12: open this action's undo macro (if it mutates the pattern and a caller wired the
        // callbacks -- see BeginMutatingActionFn's own comment in action_engine.h) BEFORE the try
        // block below, and let MutatingActionGuard close it AFTER, so every VUndoCommand a
        // handler's Create()/SaveOption()/etc. call pushes -- whether the handler returns
        // successfully or one of the catch clauses below reports a failure -- lands inside exactly
        // one macro. schema() is looked up fresh per action (not cached across the loop) since a
        // registry is immutable after construction, but this keeps the lookup colocated with its
        // one use and costs one QHash lookup per action either way (schema()'s own contract).
        const ActionSchema *schema = m_registry.schema(op);
        const bool mutates = (schema != nullptr) && schema->mutatesPattern;
        const bool macroOpened = mutates && beginMutatingAction && endMutatingAction;
        if (macroOpened)
        {
            beginMutatingAction(describeActionForMacroLabel(op, actionObject));
        }
        MutatingActionGuard macroGuard(endMutatingAction, macroOpened);

        ActionResult result; // Populated below either by the handler directly or by the ActionResolverError clause.
        try
        {
            result = handler(actionObject, ctx); // Dispatch to the registered handler with this action's fields.
        }
        catch (const ActionResolverError &error) // Thrown by NameResolver when a handler names an unknown/ambiguous/wrong-scope object.
        {
            QJsonObject errorDetail; // Structured failure so an automated (AI) caller can react to specific fields, not just parse a message string.
            errorDetail["type"] = QStringLiteral("nameResolution"); // Stable, machine-readable category for this whole error family (all three Kinds below).
            errorDetail["name"] = error.name(); // The specific name (or numeric id, as text) that failed to resolve.

            // "kind" distinguishes the three ways a lookup can fail (see ActionResolverError::Kind
            // in name_resolver.h) -- added so an automated caller can tell "this name doesn't
            // exist at all" apart from "this name exists, but only as a piece-node reference, not
            // a usable calculation-context object" (Phase 8's piece.addPatternPiece/
            // piece.internalPath legitimately produce that second situation once any piece
            // exists; a plain "unknown name" message would be actively misleading there, since
            // the name *is* known -- just not usable the way the caller intended).
            switch (error.kind())
            {
                case ActionResolverError::Kind::NotFound:
                    errorDetail["kind"] = QStringLiteral("notFound");
                    errorDetail["message"] = QStringLiteral("Unknown object name: %1").arg(error.name());
                    break;
                case ActionResolverError::Kind::WrongScope:
                    errorDetail["kind"] = QStringLiteral("wrongScope");
                    errorDetail["foundInScope"] = error.wrongScopeFoundAs(); // e.g. "modeling" -- which Draw mode the name *was* found in.
                    errorDetail["message"] = QStringLiteral(
                        "Object name \"%1\" exists, but only as a %2 object, not in the scope this action requires")
                        .arg(error.name(), error.wrongScopeFoundAs());
                    break;
                case ActionResolverError::Kind::Duplicate:
                    errorDetail["kind"] = QStringLiteral("duplicate");
                    errorDetail["message"] = QStringLiteral(
                        "Ambiguous object name (more than one match in the required scope): %1").arg(error.name());
                    break;
            }

            QStringList knownNameList = error.knownNames(); // Copy so it can be sorted without mutating the exception.
            knownNameList.sort(); // DataGObjects() is a QHash (unordered); sort so this array's order is deterministic for callers and tests.
            QJsonArray knownNames; // Converts the sorted QStringList into a JSON array.
            for (const QString &knownName : knownNameList) // Walk the sorted snapshot taken at throw time.
            {
                knownNames.append(knownName); // Append each known-good name in turn.
            }
            errorDetail["knownNames"] = knownNames; // Lets the caller self-correct without a second round trip.

            result = ActionResult::failure(QJsonValue(errorDetail)); // Structured error payload instead of a plain message string.
        }
        // Safety net, not the primary error path: every handler that reaches a real Seamly2D
        // Create()/tool call already wraps it in its own local try/catch (see e.g.
        // formula_point_handlers.cpp's runCreate()), converting VException/qmu::QmuParserError/
        // std::exception into an ActionResult::failure() before it ever reaches this loop. These
        // three clauses exist so a handler added later *without* its own local catch -- or any
        // other genuinely unanticipated failure -- still becomes a clean per-action JSON error
        // instead of an uncaught exception that would terminate the whole actiond process (losing
        // every result already gathered for earlier actions in this batch). Order matters: a
        // VException (QException subclass) is itself a std::exception, so its clause must precede
        // the std::exception clause below, exactly like the ActionResolverError clause above it.
        catch (const VException &error)
        {
            QJsonObject errorDetail;
            errorDetail["type"] = QStringLiteral("coreException"); // Distinguishes "Seamly2D's own core threw" from a plain "unhandledException" below.
            errorDetail["message"] = error.ErrorMessage();         // VException's own human-readable summary.
            const QString detail = error.DetailedInformation();    // Extra context some VException subclasses provide (e.g. the offending DOM tag); empty for most.
            if (!detail.isEmpty())
            {
                errorDetail["detail"] = detail;
            }
            result = ActionResult::failure(QJsonValue(errorDetail));
        }
        catch (const std::exception &error) // Anything else well-behaved that escaped a handler without being converted first.
        {
            QJsonObject errorDetail;
            errorDetail["type"] = QStringLiteral("unhandledException");
            errorDetail["message"] = QString::fromUtf8(error.what());
            result = ActionResult::failure(QJsonValue(errorDetail));
        }
        catch (...) // Absolute last resort: guarantees no exception of any kind escapes run()'s dispatch loop uncaught.
        {
            QJsonObject errorDetail;
            errorDetail["type"] = QStringLiteral("unknownError");
            errorDetail["message"] = QStringLiteral("An unrecognized exception escaped action '%1'").arg(op);
            result = ActionResult::failure(QJsonValue(errorDetail));
        }
        results.append(toJson(result, op)); // Record the handler's outcome, success or failure, in script order.

        if (!result.ok && abortOnFirstError) // Session-protocol "onError":"abort": stop after the first failure, keeping every result gathered so far (including this failing one).
        {
            break;
        }
    }

    QJsonObject output; // Wraps the collected results under the documented output key.
    output["results"] = results; // "results": one entry per input action, in the same order.

    return QJsonDocument(output); // Wrap the output object as the QJsonDocument callers expect.
}
