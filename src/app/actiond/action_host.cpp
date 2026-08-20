//---------------------------------------------------------------------------------------------------------------------
//  @file   action_host.cpp
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

#include "action_host.h"      // Brings in the ActionHost::runActions declaration this file implements.
#include "pattern_session.h" // Brings in PatternSession, which now owns every step this file used to inline directly (VContainer/VPattern/scene/view construction, loading, parsing).

#include "../../libs/ifc/exception/vexception.h" // Brings in VException, thrown for a save failure below (PatternSession::loadFromFile() already throws it for load/parse failures).

#include <QScopedPointer> // Provides QScopedPointer, giving the one-shot PatternSession RAII cleanup without a manual delete.

// 20 Aug, 2026: this function used to build the VContainer/VPattern/scenes/views directly (see
// git history for the original inline version); that construction-and-wiring recipe is now
// PatternSession's, shared with the new persistent NDJSON daemon mode (see session_server.cpp).
// This function's own contract -- load a pattern once, run one script, optionally save, return --
// is unchanged, so every existing caller/test of the one-shot --actions <file> CLI mode keeps
// working exactly as before.
namespace ActionHost
{
    QJsonDocument runActions(const QString &patternFilePath, const QString &measurementsFilePath,
                              const QJsonDocument &actionsScript, const QString &savePatternFilePath)
    {
        // loadFromFile() throws VException (or a subclass) on any load/parse failure -- the same
        // contract this function documented before the refactor; PatternSession's own factory
        // requires patternFilePath to exist (measurementsFilePath may be empty).
        QScopedPointer<PatternSession> session(PatternSession::loadFromFile(patternFilePath, measurementsFilePath));

        const QJsonDocument result = session->runActions(actionsScript); // abortOnFirstError left at its default (false): the one-shot CLI has always run every action and reported every result.

        // Persist whatever "basePoint"/"line"/etc. actions mutated, if the caller asked for that.
        // This runs regardless of individual actions' ok/failure -- a partially-applied batch is
        // still real DOM state worth inspecting -- and after runActions() rather than per-action,
        // so one save reflects the whole script's cumulative effect.
        if (!savePatternFilePath.isEmpty())
        {
            QString saveError; // save() reports failure via this out-parameter, not an exception.
            if (!session->save(savePatternFilePath, saveError))
            {
                throw VException(QStringLiteral("Failed to save pattern to %1: %2").arg(savePatternFilePath, saveError));
            }
        }

        return result; // {"results": [...]}, one entry per input action, independent of whether the save above ran.
    }
}
