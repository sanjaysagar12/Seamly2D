//---------------------------------------------------------------------------------------------------------------------
//  @file   piece_list_handler.h
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

#ifndef PIECE_LIST_HANDLER_H
#define PIECE_LIST_HANDLER_H

#include "../action_result.h"

class QJsonObject;
class ActionContext;

// Implements "piece.list": a read-only listing of every pattern piece currently in the pattern's
// data container, as {"pieces": [{"id","name","nodeCount","seamAllowance"}, ...]}. Never mutates
// the pattern. Added alongside "piece.dump" and render.snapshot's "target": "piece" so an AI caller
// can discover which pieces exist (and their ids/names) without guessing them out of a whole-pattern
// "pattern.dump" -- pieces are not VGObjects (VContainer::DataPieces() is a separate hash, keyed by
// id, from DataGObjects()), so pattern.dump never lists them at all.
ActionResult handlePieceList(const QJsonObject &args, const ActionContext &ctx); // Implemented in piece_list_handler.cpp.

#endif // PIECE_LIST_HANDLER_H
