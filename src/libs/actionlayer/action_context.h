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
    // Constructor stores the pointers supplied by the caller; ActionContext does not own them.
    // Phase 8: pieceScene defaults to nullptr so every pre-Phase-8 call site (action_host.cpp
    // before this change, and every ActionLayerTest fixture, which only ever passes the first
    // three positional args) keeps compiling unchanged -- piece_handlers.cpp's ops are the first
    // ones that need a second (piece-mode) scene distinct from ctx.scene()'s draft-mode one,
    // mirroring MainWindow's own separate draftScene/pieceScene split.
    ActionContext(VMainGraphicsScene *scene, VAbstractPattern *doc, VContainer *data,
                  VMainGraphicsScene *pieceScene = nullptr)
        : m_scene(scene),         // Initialize the scene pointer member from the constructor argument.
          m_doc(doc),             // Initialize the document pointer member from the constructor argument.
          m_data(data),           // Initialize the data container pointer member from the constructor argument.
          m_pieceScene(pieceScene) // Initialize the piece-scene pointer member from the constructor argument.
    {
    }

    // Getter returning the (draft-mode) graphics scene the action layer operates on.
    VMainGraphicsScene *scene() const { return m_scene; } // Returns the stored scene pointer unchanged.

    // Getter returning the pattern document the action layer operates on.
    VAbstractPattern *doc() const { return m_doc; } // Returns the stored document pointer unchanged.

    // Getter returning the variable/data container the action layer operates on.
    VContainer *data() const { return m_data; } // Returns the stored data pointer unchanged.

    // Getter returning the piece-mode graphics scene (VPiece/PatternPieceTool/InternalPathTool/
    // UnionTool all add their graphics items here, never to scene()'s draft-mode scene). May be
    // nullptr for a context built before Phase 8 or by a test that never exercises a piece op.
    VMainGraphicsScene *pieceScene() const { return m_pieceScene; } // Returns the stored piece-scene pointer unchanged.

private:
    VMainGraphicsScene *m_scene;      // Raw, non-owning pointer to the draft-mode graphics scene supplied at construction.
    VAbstractPattern   *m_doc;        // Raw, non-owning pointer to the pattern document supplied at construction.
    VContainer         *m_data;       // Raw, non-owning pointer to the variable container supplied at construction.
    VMainGraphicsScene *m_pieceScene; // Raw, non-owning pointer to the piece-mode graphics scene supplied at construction.
};

#endif // ACTION_CONTEXT_H // End of include guard started above.
