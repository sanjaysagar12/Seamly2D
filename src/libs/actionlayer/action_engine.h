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
#include <QString>       // Provides QString, the label type passed to BeginMutatingActionFn.

#include <functional> // Provides std::function, the callback types below.

class ActionRegistry; // Forward declaration; only a reference to it is stored here.
class ActionContext;  // Forward declaration; only a const reference to it is passed to run().

// ActionEngine drives execution of a parsed JSON action script against a registry and a context.
class ActionEngine
{
public:
    // Constructor stores a reference to the registry actions will be looked up in.
    explicit ActionEngine(ActionRegistry &registry); // Implemented in action_engine.cpp.

    // Phase 12 (undo/redo): optional hooks run() calls immediately before/after dispatching a
    // handler whose ActionSchema::mutatesPattern is true (see action_schema.h) -- never around a
    // read-only or session-lifecycle op. Plain std::function, not a QUndoStack-specific type or
    // signature, so ActionEngine itself stays free of any qApp/QUndoStack dependency; the one
    // real caller that supplies non-empty callbacks (PatternSession::runActions(),
    // pattern_session.cpp) is the only place that actually touches qApp->getUndoStack() --
    // matching the same "keep ActionEngine decoupled" invariant this module has held since Phase
    // 0 (see docs/ARCHITECTURE.md). beginMutatingAction receives a short diagnosable label (op
    // name plus, where available, an identifying field's value, e.g. `basePoint("A")`); intended
    // for `QUndoStack::beginMacro(label)`, surfaced later via "session.undoStatus"'s label window.
    using BeginMutatingActionFn = std::function<void(const QString &label)>;
    using EndMutatingActionFn = std::function<void()>;

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
    // beginMutatingAction/endMutatingAction default to empty (falsy) std::functions, so every
    // pre-existing call site (every ActionLayerTest fixture, which calls engine.run(script, ctx)
    // with no macro-grouping at all) keeps compiling and behaving identically -- no QUndoStack
    // macro is opened unless a caller explicitly supplies both callbacks.
    QJsonDocument run(const QJsonDocument &script, const ActionContext &ctx, bool abortOnFirstError = false,
                       const BeginMutatingActionFn &beginMutatingAction = BeginMutatingActionFn(),
                       const EndMutatingActionFn &endMutatingAction = EndMutatingActionFn()); // Implemented in action_engine.cpp.

private:
    ActionRegistry &m_registry; // Reference to the registry supplied at construction; not owned by ActionEngine.
};

#endif // ACTION_ENGINE_H // End of include guard started above.
