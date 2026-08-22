//---------------------------------------------------------------------------------------------------------------------
//  @file   piece_layout_cursor.h
//  @author Seamly2D Contributors
//  @date   22 Aug, 2026
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

#ifndef PIECE_LAYOUT_CURSOR_H
#define PIECE_LAYOUT_CURSOR_H

#include <QPointF>
#include <QRectF>
#include <qglobal.h>

// Session-lifetime layout-cursor state, owned by PatternSession (one instance per session,
// passed by non-owning pointer into ActionContext -- see that header's own "does not own them"
// convention, which this follows exactly the way ActionContext::pieceScene() already does) and
// consulted by "piece.addPatternPiece" (piece_handlers.cpp) whenever a caller omits both "mx" and
// "my". Exists because PatternPieceTool::Create() -- like the real GUI's own
// PatternPieceDialog-driven path -- never assigns a piece's mx/my beyond VPiece's
// default-constructed 0,0 (confirmed by reading pattern_piece_tool.cpp:121-183): in interactive
// use, a human drags each newly created piece to a free spot in the Piece/Layout view afterward.
// An AI-only pipeline has no equivalent step, so every piece assembled with no explicit "mx"/"my"
// would otherwise land at the exact same position -- reproduced directly (20 Aug 2026): two
// pieces built from two independently-drafted (but coordinate-overlapping) draft blocks rendered
// completely on top of each other, one entirely hiding the other.
//
// Deliberately simple: a left-to-right "shelf" pack that only needs to prevent pieces from
// literally overlapping by default, not produce a print-ready cutting layout -- a real
// bin-packing/nesting layout is a distinct, much larger feature, already flagged out of scope as
// "Phase B" work in export.scene's own schema description (action_registry.cpp).
class PieceLayoutCursor
{
public:
    // Given a new piece's raw (un-offset -- i.e. before any mx/my translation is applied) node
    // bounding rect in scene units, returns the (mx, my) offset that places it clear of every
    // piece already placed via this same cursor, and records the piece's resulting placed bounds
    // so a later call avoids it too.
    //
    // Does not (and cannot) know about a piece placed with an explicit "mx"/"my" --
    // piece_handlers.cpp only calls this when both are omitted, so an explicit placement is never
    // silently overridden, but also never recorded here either; a caller mixing explicit and
    // auto-placed pieces in one session is responsible for choosing non-colliding explicit
    // coordinates itself, the same way manually dragging some pieces and not others is in the
    // interactive GUI.
    QPointF placeNext(const QRectF &rawBounds)
    {
        // Wrap to a new row once this piece would push the current row past kRowWidth -- unless
        // the row is still empty (m_cursorX == 0.0), in which case an over-wide piece simply gets
        // its own (over-wide) row rather than wrapping forever without ever placing it.
        if (m_cursorX > 0.0 && m_cursorX + rawBounds.width() > kRowWidth)
        {
            m_cursorX = 0.0;
            m_cursorY += m_rowHeight + kMargin;
            m_rowHeight = 0.0;
        }

        // setPos(mx, my) (PatternPieceTool::RefreshGeometry(), pattern_piece_tool.cpp:1586) is an
        // absolute translation added on top of the item's own raw local (node-derived) shape, so
        // to land rawBounds' own left/top edge exactly at the current cursor position: mx/my must
        // cancel out rawBounds' own left()/top() offset, then add the cursor position on top.
        const QPointF offset(m_cursorX - rawBounds.left(), m_cursorY - rawBounds.top());

        m_cursorX += rawBounds.width() + kMargin;
        m_rowHeight = qMax(m_rowHeight, rawBounds.height());

        return offset;
    }

private:
    qreal m_cursorX = 0.0;   // Scene-x of the next free slot in the current row.
    qreal m_cursorY = 0.0;   // Scene-y of the current row's own top edge.
    qreal m_rowHeight = 0.0; // Tallest piece placed in the current row so far; decides how far down the next row starts.

    // Both arbitrary but reasonable for typical garment pieces (tens to low hundreds of scene
    // units per piece): kMargin matches render.snapshot's own default "padding"; kRowWidth is
    // generous enough that a handful of pieces fit one row before wrapping, without being so wide
    // that a real, larger pattern's pieces never wrap at all.
    static constexpr qreal kMargin = 20.0;
    static constexpr qreal kRowWidth = 1500.0;
};

#endif // PIECE_LAYOUT_CURSOR_H
