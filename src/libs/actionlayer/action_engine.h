//---------------------------------------------------------------------------------------------------------------------
//  @file   action_engine.h
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

#ifndef ACTION_ENGINE_H // Include guard start, prevents this header being processed twice in one translation unit.
#define ACTION_ENGINE_H // Marks ACTION_ENGINE_H as defined for the remainder of the include guard.

#include <QJsonDocument> // Provides QJsonDocument, both the script parameter's and run()'s return type.
#include <QString>       // Provides QString, the op-name type passed to AfterActionFn.

#include <functional> // Provides std::function, AfterActionFn's underlying type.

class ActionRegistry; // Forward declaration; only a reference to it is stored here.
class ActionContext;  // Forward declaration; only a const reference to it is passed to run().

// ActionEngine drives execution of a parsed JSON action script against a registry and a context.
class ActionEngine
{
public:
    // Constructor stores a reference to the registry actions will be looked up in.
    explicit ActionEngine(ActionRegistry &registry); // Implemented in action_engine.cpp.

    // Phase 12: optional hook run() calls immediately after each action's result is recorded, with
    // the op name and whether it succeeded -- before the loop moves on to the next action in the
    // same script. Plain std::function, not a VPattern/qApp-specific signature, so ActionEngine
    // itself stays free of any dependency beyond ActionRegistry/ActionContext (matching the same
    // "keep this module decoupled" invariant docs/ARCHITECTURE.md's ADR already holds). The one
    // real caller that supplies a non-empty callback is PatternSession::runActions()
    // (src/app/actiond/pattern_session.cpp): "pattern.undo" (history_undo_handlers.cpp) only
    // removes DOM elements -- it cannot itself trigger the VPattern::Parse(Document::FullParse)
    // that makes those removals visible to VContainer/the scenes (VPattern::Parse() is not
    // reachable through the VAbstractPattern* pointer handlers are given -- see history_undo_handlers.h's own
    // header comment for why) -- so PatternSession's callback does that reparse right here, immediately,
    // rather than once at the very end of the whole script. That immediacy is load-bearing, not a
    // style choice: a later action in the SAME script (e.g. a "pattern.dump" right after a
    // "pattern.undo") must see the already-pruned state, not whatever was live before the reparse
    // -- deferring the reparse to end-of-batch was tried first and empirically failed exactly this
    // case (verified via a real actiond run during development; see CHANGELOG.md's Phase 12 entry).
    using AfterActionFn = std::function<void(const QString &op, bool ok)>;

    // Parses the top-level {"actions": [{"op": "...", ...}, ...]} script, dispatches each entry
    // through the registry, and returns {"results": [...]} with one entry per action, in order.
    // An unknown "op" produces a result entry with ok == false instead of crashing or skipping.
    //
    // abortOnFirstError (default false, preserving every pre-existing call site's behavior):
    // when true, the loop appends the result for the first failing action and then stops --
    // "results" holds one entry per action actually processed, not one per input action. Backs
    // the NDJSON session protocol's "onError":"abort" (see session_server.cpp); the default-false
    // "keep going" behavior remains what every one-shot actiond script and ActionLayerTest fixture
    // already relies on.
    //
    // afterAction defaults to an empty (falsy) std::function, so every pre-existing call site
    // (every ActionLayerTest fixture, which calls engine.run(script, ctx) with no hook at all)
    // keeps compiling and behaving identically.
    QJsonDocument run(const QJsonDocument &script, const ActionContext &ctx, bool abortOnFirstError = false,
                       const AfterActionFn &afterAction = AfterActionFn()); // Implemented in action_engine.cpp.

private:
    ActionRegistry &m_registry; // Reference to the registry supplied at construction; not owned by ActionEngine.
};

#endif // ACTION_ENGINE_H // End of include guard started above.
