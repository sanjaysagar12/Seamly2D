//---------------------------------------------------------------------------------------------------------------------
//  @file   action_host.h
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

#ifndef ACTION_HOST_H // Include guard start, prevents this header being processed twice in one translation unit.
#define ACTION_HOST_H // Marks ACTION_HOST_H as defined for the remainder of the include guard.

#include <QJsonDocument> // Provides QJsonDocument, both the actions-script parameter type and runActions()'s return type.
#include <QString>       // Provides QString, the type of the two file path parameters.

// ActionHost owns the one-shot "load a pattern, load its measurements, run a script" pipeline
// actiond's main() delegates to. Kept separate from main.cpp so argument parsing/error-formatting
// stay decoupled from the actual Seamly2D loading sequence.
namespace ActionHost
{
    // Loads the pattern file and measurement file, wires them into an ActionContext exactly as
    // MainWindow::LoadPattern() would, runs actionsScript through ActionEngine, and returns its
    // result document. Throws VException (or a subclass) -- the same exception type every reused
    // Seamly2D loading call already throws -- on any load or parse failure; callers must catch it.
    QJsonDocument runActions(const QString &patternFilePath, const QString &measurementsFilePath,
                              const QJsonDocument &actionsScript);
}

#endif // ACTION_HOST_H // End of include guard started above.
