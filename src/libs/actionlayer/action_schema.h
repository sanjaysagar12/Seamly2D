//---------------------------------------------------------------------------------------------------------------------
//  @file   action_schema.h
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

#ifndef ACTION_SCHEMA_H // Include guard start, prevents this header being processed twice in one translation unit.
#define ACTION_SCHEMA_H // Marks ACTION_SCHEMA_H as defined for the remainder of the include guard.

#include <QString> // Provides QString, used for every text field below.
#include <QVector> // Provides QVector, used for ActionSchema::parameters.

// Describes one JSON field a registered action accepts. This is descriptive metadata only -- it is
// never used to validate/parse an incoming action at dispatch time (each handler still does that
// itself, exactly as before); it exists purely so `actiond --list-tools --format=human`/`--format=ai` (see tool_catalog.h) and
// a person reading action_registry.cpp can see every op's real parameters in one place, generated
// from the same registration call that wires up the handler function itself -- see this file's own
// header comment in action_registry.cpp for why that single-source-of-truth property matters.
struct ActionParamSchema
{
    QString name;        // The JSON field name a caller writes, e.g. "basePoint" or "length".
    QString jsonType;     // JSON Schema primitive this field's value must be: "string", "number", "boolean", "array", or "object".
    bool required = false; // Whether the handler's own field-validation code rejects a request missing this field.

    // Human-readable explanation of what this field means and how the handler treats it. For any
    // string field the handler passes through Seamly2D's formula engine (VAbstractTool::
    // CheckFormula()/Calculator::EvalFormula()) rather than treating as a literal value, this MUST
    // say so explicitly (e.g. "Formula string evaluated by Seamly2D's formula engine (e.g. '10' or
    // 'neck_width/2').") -- JSON Schema has no native "formula" type, so this description field is
    // the only place that distinction is recorded for an AI/automated caller reading only the
    // AI-format tool list.
    QString description;

    QString defaultValue; // The literal default the handler applies when this field is omitted, as text (e.g. "0", "true", "solidLine"); empty when the field has no default (required fields always leave this empty).

    // Only meaningful when jsonType == "array": the JSON type of each array element ("string" or
    // "object"), so a caller building a JSON Schema "items" clause knows what belongs inside.
    QString itemsType;
};

// Describes one registered action op: what it does, every field it accepts, one worked example,
// and (for the two documented partial-coverage ops) why it is only partially usable. Built once per
// op, at the exact call site that registers its handler function in ActionRegistry -- see
// action_registry.cpp's registerBuiltinActions() -- so a new op cannot be registered without also
// supplying this metadata (ActionRegistry::registerAction() requires an ActionSchema argument).
struct ActionSchema
{
    QString op;           // The exact string a caller writes as the action's "op" field, e.g. "endLine" or "piece.addPatternPiece".
    QString category;     // Groups this op for the human-readable listing, matching docs/action-layer-schema.md's own section headings: "introspection", "point", "formula-point", "curve", "cut-point", "operation", "piece", "measurements", "session".
    QString description;  // One or two sentences: what this action actually does when it succeeds.
    QVector<ActionParamSchema> parameters; // Every field this op accepts, in the order a person would naturally fill them in.
    QString exampleRequest; // A real, valid example JSON object for this op (a complete `{"op": ..., ...}` request), copied from (and kept in sync with) docs/action-layer-schema.md's own worked examples.

    // True for an op with a real, documented behavioral gap (currently only piece.union and
    // piece.insertNodes -- see docs/action-layer-schema.md's own KNOWN GAP notes on each). This
    // metadata is otherwise complete for both; "partial" here describes the *op's execution*, not a
    // hole in this descriptive data, and both `--format=human`/`--format=ai` output formats surface it explicitly rather
    // than silently listing the op as if it were fully reliable.
    bool partial = false;
    QString partialReason; // Human-readable reason, only meaningful when partial == true.
};

#endif // ACTION_SCHEMA_H // End of include guard started above.
