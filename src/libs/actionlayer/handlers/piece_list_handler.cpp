//---------------------------------------------------------------------------------------------------------------------
//  @file   piece_list_handler.cpp
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

#include "piece_list_handler.h" // Brings in the ActionResult-returning handlePieceList declaration this file implements.

#include "../action_context.h" // Brings in ActionContext, supplying the data container this handler reads.

#include "../../vpatterndb/vcontainer.h" // Brings in VContainer::DataPieces(), the source of every pattern piece.
#include "../../vpatterndb/vpiece.h"     // Brings in VPiece: GetName(), GetPath(), hasSeamAllowance().
#include "../../vpatterndb/vpiecepath.h" // Brings in VPiecePath::nodeCount(), read from each piece's own main path.

#include <QJsonArray>  // Provides QJsonArray, used to build the "pieces" JSON array.
#include <QJsonObject> // Provides QJsonObject, used for each per-piece JSON record.
#include <QVector>     // Provides QVector, used to sort DataPieces()'s ids before iterating (see below).
#include <algorithm>   // Provides std::sort.

// Implements "piece.list": see piece_list_handler.h for the documented output shape.
ActionResult handlePieceList(const QJsonObject &args, const ActionContext &ctx)
{
    Q_UNUSED(args) // piece.list takes no arguments; kept in the signature for uniformity with other handlers.

    QJsonArray pieces; // Accumulates one JSON object per piece found in the container.

    const VContainer *data = ctx.data(); // Local alias for the pattern's variable/data container.
    if (data != nullptr) // Guard against a context that was constructed without a data container.
    {
        const QHash<quint32, VPiece> *dataPieces = data->DataPieces(); // Every piece, keyed by id.
        if (dataPieces != nullptr) // DataPieces() can return nullptr before a pattern has been parsed.
        {
            // Same QHash-iteration-order determinism fix pattern.dump's own "objects" array needed
            // (see pattern_dump_handler.cpp's own comment on it, and tests/actionlayer/README.md's
            // "Known gaps" section): QHash's iteration order is randomized per process, so walking
            // dataPieces directly would make this op's own array order differ between two otherwise-
            // identical runs of the same script against the same fixture. Sorting by id first makes
            // the output deterministic without changing which pieces are reported.
            QVector<quint32> ids;
            ids.reserve(dataPieces->size());
            for (auto it = dataPieces->constBegin(); it != dataPieces->constEnd(); ++it)
            {
                ids.append(it.key());
            }
            std::sort(ids.begin(), ids.end());

            for (quint32 id : ids) // Walk every entry in id order.
            {
                const VPiece &piece = dataPieces->value(id); // QHash::value() returns by value; the const& below extends that temporary's lifetime to this loop body.

                QJsonObject entry; // Builds this one piece's JSON record.
                entry["id"] = static_cast<qint64>(id); // The piece's own id is the DataPieces() hash key -- the same id piece.addPatternPiece's own response reports, and piece.addAnchorPoint/internalPath/insertNodes/union resolve their "piece" name argument to internally.
                entry["name"] = piece.GetName(); // The piece's user-assigned name.
                entry["nodeCount"] = piece.GetPath().nodeCount(); // Number of nodes in the piece's main outline path (not counting internal paths/anchors).
                entry["seamAllowance"] = piece.hasSeamAllowance(); // Whether seam allowance is enabled for this piece (independent of its width formula -- see piece.addPatternPiece's own "seamAllowance" param doc).
                pieces.append(entry); // Add this piece's JSON record to the output array.
            }
        }
    }

    QJsonObject payload; // Combines the array under its documented output key.
    payload["pieces"] = pieces; // "pieces": every pattern piece currently in the data container.

    return ActionResult::success(payload); // Wrap the payload as a successful result for the registry/engine to return.
}
