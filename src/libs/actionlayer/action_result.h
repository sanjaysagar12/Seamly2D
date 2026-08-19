//---------------------------------------------------------------------------------------------------------------------
//  @file   action_result.h
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

#ifndef ACTION_RESULT_H // Include guard start, prevents this header being processed twice in one translation unit.
#define ACTION_RESULT_H // Marks ACTION_RESULT_H as defined for the remainder of the include guard.

#include <QJsonValue> // Provides QJsonValue, the type of both the success payload and (as of Phase 4) the failure detail.
#include <QString>    // Provides QString, the type of the plain-message failure() overload's parameter.

// ActionResult is the uniform return type every action handler produces: either a JSON payload
// on success, or an error on failure. It intentionally has no logic of its own beyond a few small
// factory helpers, so callers can pattern-match on "ok" without reaching into a variant.
struct ActionResult
{
    bool ok = false;                      // True when the handler succeeded; false signals the "error" field is meaningful.
    QJsonValue value;                     // The handler's JSON payload on success; left null/default on failure.
    QJsonValue error = QJsonValue(QString()); // Failure detail: a plain string for most handlers, or a structured object (see below). Empty string by default, matching a successful result's prior plain-string shape.

    // Builds a successful result carrying the given JSON payload.
    static ActionResult success(const QJsonValue &value)
    {
        ActionResult result;  // Default-constructs with ok == false until overwritten below.
        result.ok = true;     // Mark this result as successful.
        result.value = value; // Store the caller-supplied payload.
        return result;        // Return the populated result by value.
    }

    // Builds a failed result carrying a plain human-readable error message. This remains the
    // common case for most handlers; QString converts implicitly to QJsonValue, so this overload
    // is selected ahead of the QJsonValue one below whenever the caller passes a QString.
    static ActionResult failure(const QString &error)
    {
        ActionResult result;  // Default-constructs with ok == false, which is already what we want.
        result.error = error; // Store the caller-supplied error message.
        return result;        // Return the populated result by value.
    }

    // Builds a failed result carrying a structured JSON error (e.g. {"type": "nameResolution", ...}).
    // Added in Phase 4 so ActionEngine's ActionResolverError catch clause can report typed fields
    // (name, knownNames) instead of flattening everything into one message string.
    static ActionResult failure(const QJsonValue &error)
    {
        ActionResult result; // Default-constructs with ok == false, which is already what we want.
        result.error = error; // Store the caller-supplied structured error.
        return result;        // Return the populated result by value.
    }
};

#endif // ACTION_RESULT_H // End of include guard started above.
