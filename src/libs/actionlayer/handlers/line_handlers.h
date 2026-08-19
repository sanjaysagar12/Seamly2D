//---------------------------------------------------------------------------------------------------------------------
//  @file   line_handlers.h
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

#ifndef LINE_HANDLERS_H // Include guard start, prevents this header being processed twice in one translation unit.
#define LINE_HANDLERS_H // Marks LINE_HANDLERS_H as defined for the remainder of the include guard.

#include "../action_result.h" // Provides ActionResult, this handler's return type.

class QJsonObject;   // Forward declaration; only used by const reference in the signature below.
class ActionContext; // Forward declaration; only used by const reference in the signature below.

// Implements the "line" op: connects two already-named points with a VToolLine, resolving both
// names through NameResolver (Phase 4) so an unknown name surfaces as ActionEngine's existing
// structured nameResolution error rather than a new, differently-shaped one.
ActionResult handleLine(const QJsonObject &args, const ActionContext &ctx); // Implemented in line_handlers.cpp.

#endif // LINE_HANDLERS_H // End of include guard started above.
