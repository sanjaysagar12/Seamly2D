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

#include <QJsonArray>    // Provides QJsonArray, the run() return type and the type of the "actions" element.
#include <QJsonDocument> // Provides QJsonDocument, the type of the script parameter passed to run().

class ActionRegistry; // Forward declaration; only a reference to it is stored here.
class ActionContext;  // Forward declaration; only a reference to it is passed to run().

// ActionEngine drives execution of a parsed JSON action script against a registry and a context.
class ActionEngine
{
public:
    // Constructor stores a reference to the registry actions will be looked up in during later phases.
    explicit ActionEngine(ActionRegistry &registry); // Implemented in action_engine.cpp.

    // Parses the top-level "actions" array from the script. Phase 0 does not dispatch anything yet.
    QJsonArray run(const QJsonDocument &script, ActionContext &ctx); // Implemented in action_engine.cpp.

private:
    ActionRegistry &m_registry; // Reference to the registry supplied at construction; not owned by ActionEngine.
};

#endif // ACTION_ENGINE_H // End of include guard started above.
