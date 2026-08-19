//---------------------------------------------------------------------------------------------------------------------
//  @file   action_context.h
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

#ifndef ACTION_CONTEXT_H // Include guard start, prevents this header being processed twice in one translation unit.
#define ACTION_CONTEXT_H // Marks ACTION_CONTEXT_H as defined for the remainder of the include guard.

class VMainGraphicsScene; // Forward declaration avoids pulling in the full vwidgets scene header here.
class VAbstractPattern;   // Forward declaration avoids pulling in the full ifc document header here.
class VContainer;         // Forward declaration avoids pulling in the full vpatterndb container header here.

// ActionContext bundles the scene, document, and data container an action needs to read or mutate the pattern.
class ActionContext
{
public:
    // Constructor stores the three pointers supplied by the caller; ActionContext does not own them.
    ActionContext(VMainGraphicsScene *scene, VAbstractPattern *doc, VContainer *data)
        : m_scene(scene), // Initialize the scene pointer member from the constructor argument.
          m_doc(doc),     // Initialize the document pointer member from the constructor argument.
          m_data(data)    // Initialize the data container pointer member from the constructor argument.
    {
    }

    // Getter returning the graphics scene the action layer operates on.
    VMainGraphicsScene *scene() const { return m_scene; } // Returns the stored scene pointer unchanged.

    // Getter returning the pattern document the action layer operates on.
    VAbstractPattern *doc() const { return m_doc; } // Returns the stored document pointer unchanged.

    // Getter returning the variable/data container the action layer operates on.
    VContainer *data() const { return m_data; } // Returns the stored data pointer unchanged.

private:
    VMainGraphicsScene *m_scene; // Raw, non-owning pointer to the graphics scene supplied at construction.
    VAbstractPattern   *m_doc;   // Raw, non-owning pointer to the pattern document supplied at construction.
    VContainer         *m_data;  // Raw, non-owning pointer to the variable container supplied at construction.
};

#endif // ACTION_CONTEXT_H // End of include guard started above.
