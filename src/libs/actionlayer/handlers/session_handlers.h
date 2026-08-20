//---------------------------------------------------------------------------------------------------------------------
//  @file   session_handlers.h
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

#ifndef SESSION_HANDLERS_H // Include guard start, prevents this header being processed twice in one translation unit.
#define SESSION_HANDLERS_H // Marks SESSION_HANDLERS_H as defined for the remainder of the include guard.

#include "../action_result.h" // Provides ActionResult, the return type both handlers below produce.

class QJsonObject;   // Forward declaration; only used by const reference in the signatures below.
class ActionContext; // Forward declaration; only used by const reference in the signatures below.

// Implements "session.save": {"path"} -> {"path"}. Writes the current pattern document to path
// (resolved relative to the process's current working directory -- the persistent NDJSON daemon
// sets that to --output-dir at startup, so a relative path here lands there, with zero handler-
// side path-juggling) via VDomDocument::SaveDocument(), the same call the one-shot CLI's
// --save-pattern flag has always used. This is the in-script equivalent of that flag: unlike
// --save-pattern, it can be issued mid-batch, more than once, or not at all, from inside an NDJSON
// request.
ActionResult handleSessionSave(const QJsonObject &args, const ActionContext &ctx); // Implemented in session_handlers.cpp.

// Implements "session.close": {} -> {"closing": true}. Takes no parameters and always succeeds;
// its only real effect is on the *caller* (SessionServer), which -- after this action's result is
// recorded like any other -- exits its read loop once the current batch's response has been
// written. Registered as a normal action (rather than only special-cased in the protocol loop) so
// it participates in the ordinary results/appliedCount bookkeeping and shows up in
// "pattern.listTools" like every other op.
ActionResult handleSessionClose(const QJsonObject &args, const ActionContext &ctx); // Implemented in session_handlers.cpp.

#endif // SESSION_HANDLERS_H // End of include guard started above.
