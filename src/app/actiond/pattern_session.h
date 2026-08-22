//---------------------------------------------------------------------------------------------------------------------
//  @file   pattern_session.h
//  @author Seamly2D Contributors
//  @date   20 Aug, 2026
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

#ifndef PATTERN_SESSION_H // Include guard start, prevents this header being processed twice in one translation unit.
#define PATTERN_SESSION_H // Marks PATTERN_SESSION_H as defined for the remainder of the include guard.

#include "../../libs/actionlayer/action_context.h"      // Provides ActionContext, returned by context() below.
#include "../../libs/actionlayer/action_registry.h"     // Provides ActionRegistry, owned so every built-in handler is available.
#include "../../libs/actionlayer/action_engine.h"       // Provides ActionEngine, owned to dispatch runActions()'s script.
#include "../../libs/actionlayer/piece_layout_cursor.h" // Provides PieceLayoutCursor, owned so piece.addPatternPiece's auto-placement accumulates across this whole session -- a value member (not a pointer), so the full definition (not just a forward declaration) is required here.

#include <QJsonDocument>  // Provides QJsonDocument, both runActions()'s parameter and return type.
#include <QScopedPointer> // Provides QScopedPointer, used to own the VPattern document.
#include <QString>        // Provides QString, used throughout for file paths and error output parameters.

class VContainer;         // Forward declaration; full definition only needed in the .cpp.
class VPattern;            // Forward declaration; full definition only needed in the .cpp (src/app/seamly2d/xml/vpattern.h).
class VMainGraphicsScene; // Forward declaration; full definition only needed in the .cpp.
class VMainGraphicsView;  // Forward declaration; full definition only needed in the .cpp.
class QGraphicsScene;     // Forward declaration; full definition only needed in the .cpp.

// PatternSession owns the long-lived state one loaded (or freshly created) pattern needs to keep
// running action batches against: the VContainer data, the VPattern document, the draft/piece
// scenes, and the (never shown) views those scenes need attached. This is the same
// construction-and-wiring recipe action_host.cpp's one-shot ActionHost::runActions() originally
// inlined (several of the steps -- qApp->setCurrentScene()/setSceneView(), attaching a view to
// each scene -- are load-bearing workarounds for reused undo-command redo() paths and paint()
// overrides that null-deref/hit undefined behavior without them; see the .cpp for the full
// rationale behind each one), pulled out here so it has exactly one implementation shared by:
//   - One-shot (ActionHost::runActions(), the pre-existing actiond --actions <file> CLI mode):
//     construct, run exactly one script, optionally save, destroy.
//   - Persistent (SessionServer, the new NDJSON daemon mode): construct once at process start,
//     then call runActions() once per NDJSON request line for the rest of the process's life.
//
// VPattern (not VAbstractPattern) is deliberately not part of the actionlayer static library --
// it is compiled directly into actiond's own SOURCES (see actiond.pro), the same place
// action_host.cpp already lived -- so PatternSession lives here, alongside it, rather than in
// src/libs/actionlayer/ alongside the (VPattern-agnostic) action handlers.
class PatternSession
{
public:
    // Loads patternFilePath (a .val file) via the same VPatternConverter + VPattern::setXMLContent
    // + VPattern::Parse(Document::FullParse) recipe MainWindow::LoadPattern() itself uses.
    // measurementsFilePath may be empty: when it is, the whole MeasurementDoc step is skipped
    // (formulas referencing measurement variables will then fail to evaluate at the point they're
    // used -- an expected, reported-as-a-normal-action-failure outcome, not a crash). Throws
    // VException (or a subclass) on any load/parse failure, exactly as action_host.cpp did before
    // this refactor.
    static PatternSession *loadFromFile(const QString &patternFilePath, const QString &measurementsFilePath);

    // Builds the same VContainer/VPattern/scenes/views wiring as loadFromFile(), but instead of
    // loading an existing file, calls VPattern::CreateEmptyFile() -- the minimal "new pattern" DOM
    // (root <pattern> element, version, unit, empty <measurements>/<variables> sections, no draft
    // blocks yet) that MainWindow::New()'s dialog-driven path ultimately relies on. No Parse()
    // call is made (there is nothing in the DOM to parse yet); the first "basePoint" action's own
    // doc->appendDraftBlock() call creates the first draft block, exactly as it already does for a
    // loaded file (see src/libs/actionlayer/handlers/point_handlers.cpp). measurementsFilePath may
    // be empty, with the same meaning as loadFromFile()'s.
    static PatternSession *createEmpty(const QString &measurementsFilePath);

    ~PatternSession();

    // Runs script through this session's ActionEngine/ActionRegistry, returning
    // {"results": [...]} exactly as ActionEngine::run() itself documents. abortOnFirstError is
    // forwarded verbatim (see action_engine.h); defaults to false so a one-shot caller's existing
    // "run everything, report every result" behavior is unchanged.
    QJsonDocument runActions(const QJsonDocument &script, bool abortOnFirstError = false);

    // Writes this session's current pattern document to path via VDomDocument::SaveDocument() (the
    // same call action_host.cpp made via its --save-pattern flag). Returns false and sets error on
    // failure, exactly matching SaveDocument()'s own out-parameter contract -- never throws.
    bool save(const QString &path, QString &error);

    // Exposes the ActionContext handlers dispatch against, for a caller (SessionServer) that needs
    // it directly rather than only through runActions()/save().
    const ActionContext &context() const;

private:
    // Shared construction: patternFilePath empty means "start from CreateEmptyFile() instead of
    // loading a file" (createEmpty()'s path); otherwise loads and fully parses patternFilePath
    // (loadFromFile()'s path). measurementsFilePath empty in either case means "skip MeasurementDoc
    // entirely". Only the two static factories above construct a PatternSession, so every instance
    // is guaranteed to have gone through this one well-defined setup path.
    PatternSession(const QString &patternFilePath, const QString &measurementsFilePath);
    Q_DISABLE_COPY(PatternSession)

    VContainer *m_data;                  // Owned; the pattern's variable/geometry-object container.
    VMainGraphicsScene *m_draftScene;    // Owned; the draft-mode scene (base points, lines, curves, ...).
    VMainGraphicsScene *m_pieceScene;    // Owned; the piece-mode scene (cut pieces built from the draft).
    VMainGraphicsView *m_sceneView;      // Owned; never shown -- exists only so qApp->getSceneView()/draftScene.views() are non-empty (see .cpp).
    VMainGraphicsView *m_pieceSceneView; // Owned; never shown -- exists only so pieceScene.views() is non-empty (see .cpp).
    QGraphicsScene *m_currentScene;      // Owned indirectly (aliases m_draftScene); its ADDRESS is handed to qApp->setCurrentScene(), so it must live exactly as long as this session, not just through construction.
    QScopedPointer<VPattern> m_doc;      // Owned; the pattern document.

    ActionRegistry m_registry; // Owned; auto-registers every built-in action handler.
    ActionEngine m_engine;     // Owned; dispatches runActions()'s script through m_registry.
    PieceLayoutCursor m_pieceLayoutCursor; // Owned; must be declared (and so constructed) before m_context below, whose constructor takes this member's address -- C++ initializes members in declaration order, not initializer-list order.
    ActionContext m_context;   // Owned; bundles m_draftScene/m_doc/m_data/m_pieceScene/&m_pieceLayoutCursor for handlers.
};

#endif // PATTERN_SESSION_H // End of include guard started above.
