//---------------------------------------------------------------------------------------------------------------------
//  @file   action_registry.cpp
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

#include "action_registry.h" // Brings in the ActionRegistry class declaration this file implements.

// Stores the handler function in the internal map under the given name.
void ActionRegistry::registerAction(const QString &name, ActionFn fn)
{
    m_actions.insert(name, fn); // QHash::insert adds the entry, replacing any existing one with the same key.
}

// Reports whether a handler has been registered for the given name.
bool ActionRegistry::hasAction(const QString &name) const
{
    return m_actions.contains(name); // QHash::contains performs the presence check.
}
