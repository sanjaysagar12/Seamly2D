//---------------------------------------------------------------------------------------------------------------------
//  @file   action_error.h
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

#ifndef ACTION_ERROR_H // Include guard start, prevents this header being processed twice in one translation unit.
#define ACTION_ERROR_H // Marks ACTION_ERROR_H as defined for the remainder of the include guard.

#include <QString> // Provides QString, the type of both the message and the operation name below.

// ActionError carries a structured failure: a human-readable message plus the op name it came
// from, if known. It is not yet consumed anywhere in Phase 1 -- ActionResult::error remains a
// plain QString for now -- but exists as the return-type building block a future phase can use
// for structured error reporting without throwing exceptions across Qt slot boundaries.
class ActionError
{
public:
    // Default constructor leaves both fields empty; used when no error has occurred yet.
    ActionError() = default; // Compiler-generated default construction is sufficient here.

    // Constructs an error from a message and the (optional) action op that produced it.
    explicit ActionError(const QString &message, const QString &actionOp = QString())
        : m_message(message), // Initialize the message member from the constructor argument.
          m_actionOp(actionOp) // Initialize the action-op member from the constructor argument, defaulting to empty.
    {
    }

    // Getter returning the human-readable failure message.
    QString message() const { return m_message; } // Returns the stored message unchanged.

    // Getter returning the op name this error came from, or an empty string if not set.
    QString actionOp() const { return m_actionOp; } // Returns the stored action op unchanged.

private:
    QString m_message;  // Human-readable description of what went wrong.
    QString m_actionOp; // Name of the action op that produced this error, if known.
};

#endif // ACTION_ERROR_H // End of include guard started above.
