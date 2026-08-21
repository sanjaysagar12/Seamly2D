//---------------------------------------------------------------------------------------------------------------------
//  @file   tool_catalog.h
//  @author Seamly2D Contributors
//  @date   21 Aug, 2026
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

#ifndef TOOL_CATALOG_H // Include guard start, prevents this header being processed twice in one translation unit.
#define TOOL_CATALOG_H // Marks TOOL_CATALOG_H as defined for the remainder of the include guard.

#include <QJsonArray> // Provides QJsonArray, the return type of renderAi().
#include <QString>    // Provides QString, the return type of renderHuman().

class ActionRegistry; // Forward declaration; both functions below only need a const reference to read its schemas.

// Renders ActionRegistry::allSchemas() into the two output formats `actiond --list-tools` supports (see
// main.cpp's --list-tools/--format handling). Pure functions of the registry's own schema data --
// neither touches a pattern, scene, or any other runtime state, matching the "runnable standalone,
// no pattern required" requirement `actiond --list-tools` was built around.
namespace ToolCatalog
{
    // Human-readable text: grouped by category (matching docs/action-layer-schema.md's own section
    // headings), each op showing its name, description, a parameter table, and a worked example.
    // Plain text, no ANSI escapes, safe to print directly to a terminal or redirect to a file.
    QString renderHuman(const ActionRegistry &registry);

    // Machine-readable: one JSON array, shaped like a standard LLM tool-definition list -- each
    // entry `{"name", "description", "input_schema": {"type": "object", "properties": {...},
    // "required": [...]}}`, plus this action layer's own "category" field and (only when true) a
    // "status": "partial" field for the two ops with a documented behavioral gap. Every entry in
    // registry.allSchemas() gets exactly one array entry -- an incomplete/partial op is still
    // listed (marked, not omitted), per the "no silent gaps" requirement this command was built to
    // satisfy.
    QJsonArray renderAi(const ActionRegistry &registry);
}

#endif // TOOL_CATALOG_H // End of include guard started above.
