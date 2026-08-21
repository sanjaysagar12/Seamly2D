//---------------------------------------------------------------------------------------------------------------------
//  @file   action_registry.cpp
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

#include "action_registry.h" // Brings in the ActionRegistry class declaration this file implements.

#include "handlers/pattern_dump_handler.h"         // Brings in handlePatternDump(), registered under "pattern.dump".
#include "handlers/pattern_measurements_handler.h" // Brings in handleListMeasurements(), registered under "pattern.listMeasurements".
#include "handlers/pattern_list_tools_handler.h"   // Brings in handleListTools(), registered under "pattern.listTools".
#include "handlers/render_handlers.h"              // Brings in handleRenderSnapshot(), registered under "render.snapshot".
#include "handlers/export_handlers.h"              // Brings in handleExportScene(), registered under "export.scene".
#include "handlers/pattern_resolve_name_handler.h" // Brings in handlePatternResolveName(), registered under "pattern.resolveName".
#include "handlers/point_handlers.h"                // Brings in handleBasePoint(), registered under "basePoint".
#include "handlers/line_handlers.h"                 // Brings in handleLine(), registered under "line".
#include "handlers/formula_point_handlers.h"        // Brings in the six Phase 6 handle*() functions below (endLine, alongLine, normal, bisector, shoulderPoint, lineIntersect).
#include "handlers/measurements_sync_handlers.h"    // Brings in the three Phase 7 handle*() functions below (measurements.load, .recompute, .sync).
#include "handlers/curve_handlers.h"                 // Brings in the seven Phase 8 curve handle*() functions below.
#include "handlers/cutpoint_handlers.h"               // Brings in the ten Phase 8 cut/intersection point handle*() functions below.
#include "handlers/operation_handlers.h"              // Brings in the six Phase 8 operation handle*() functions below (move, rotation, mirrorByLine, mirrorByAxis, group, trueDarts).
#include "handlers/piece_handlers.h"                  // Brings in the five Phase 8 piece.* handle*() functions below.
#include "handlers/point_edit_handlers.h"              // Brings in handlePointEdit(), registered under "point.edit".
#include "handlers/session_handlers.h"                 // Brings in handleSessionSave()/handleSessionClose(), registered under "session.save"/"session.close".

#include <algorithm> // Provides std::sort, used by allSchemas() to return a deterministically ordered list.
#include <utility>   // Provides std::move, used by the buildSchema() builder below.

namespace
{
    // Small builder so each ActionParamSchema literal below reads as one line instead of a
    // multi-line brace-init. `defaultValue`/`itemsType` are optional (empty string when unused).
    ActionParamSchema param(const QString &name, const QString &jsonType, bool required,
                            const QString &description, const QString &defaultValue = QString(),
                            const QString &itemsType = QString())
    {
        ActionParamSchema p;
        p.name = name;
        p.jsonType = jsonType;
        p.required = required;
        p.description = description;
        p.defaultValue = defaultValue;
        p.itemsType = itemsType;
        return p;
    }

    // Small builder so each registerAction(...) call below can pass its ActionSchema as one
    // readable expression. `partial`/`partialReason` are optional (false/empty for the 45 ops with
    // no documented behavioral gap).
    ActionSchema buildSchema(const QString &op, const QString &category, const QString &description,
                        QVector<ActionParamSchema> parameters, const QString &exampleRequest,
                        bool partial = false, const QString &partialReason = QString())
    {
        ActionSchema s;
        s.op = op;
        s.category = category;
        s.description = description;
        s.parameters = std::move(parameters);
        s.exampleRequest = exampleRequest;
        s.partial = partial;
        s.partialReason = partialReason;
        return s;
    }
}

// Constructs an empty handler map, then immediately populates it with every built-in handler.
ActionRegistry::ActionRegistry()
{
    registerBuiltinActions(); // Every freshly constructed registry is ready to use without extra setup calls.
}

// Registers every built-in handler under its documented op name, together with the ActionSchema
// describing its parameters (see action_schema.h). This is the one place that lists every currently
// registered op and its metadata in the same statement -- pattern_list_tools_handler.cpp keeps its
// own separate, hand-written mirror of just the op names for the "pattern.listTools" response (a
// pre-existing op this refresh does not change), and docs/action-layer-schema.md is a browsable
// snapshot of the same information; `actiond --list-tools --format=human`/`--format=ai` (tool_catalog.cpp) is generated
// directly from the schemas below, so it cannot drift from what is actually registered here the way
// pattern_list_tools_handler.cpp's separate list once did (see docs/action-layer-schema.md's own
// "Findings" section for that incident).
void ActionRegistry::registerBuiltinActions()
{
    // ---- Read-only introspection --------------------------------------------------------------
    registerAction(QStringLiteral("pattern.dump"), &handlePatternDump, buildSchema(
        QStringLiteral("pattern.dump"), QStringLiteral("introspection"),
        QStringLiteral("Reads every geometry object currently in the pattern's data container, plus the tool-creation history. Read-only; never mutates the pattern."),
        {},
        QStringLiteral(R"({ "op": "pattern.dump" })")));

    registerAction(QStringLiteral("pattern.listMeasurements"), &handleListMeasurements, buildSchema(
        QStringLiteral("pattern.listMeasurements"), QStringLiteral("introspection"),
        QStringLiteral("Reads every measurement variable currently attached to the pattern (name, formula, resolved value). An empty array is a valid result, not an error."),
        {},
        QStringLiteral(R"({ "op": "pattern.listMeasurements" })")));

    registerAction(QStringLiteral("pattern.listTools"), &handleListTools, buildSchema(
        QStringLiteral("pattern.listTools"), QStringLiteral("introspection"),
        QStringLiteral("Returns a static, hand-maintained list of op names this action layer supports. Prefer `actiond --list-tools --format=ai` for a live, schema-complete listing generated from the same registry this op reads a separate hand-written mirror of."),
        {},
        QStringLiteral(R"({ "op": "pattern.listTools" })")));

    registerAction(QStringLiteral("render.snapshot"), &handleRenderSnapshot, buildSchema(
        QStringLiteral("render.snapshot"), QStringLiteral("introspection"),
        QStringLiteral("Rasterizes the current draft scene to an image file, optionally highlighting named objects with a translucent overlay. Read-only with respect to pattern data; only writes the image file itself."),
        {
            param(QStringLiteral("path"), QStringLiteral("string"), true,
                QStringLiteral("Output file path. Parent directory created if missing.")),
            param(QStringLiteral("target"), QStringLiteral("string"), false,
                QStringLiteral("Which scene to render. \"draft\" is the only value accepted today -- ActionContext exposes no piece scene to this op yet; anything else is a hard error, not a silent fallback."),
                QStringLiteral("draft")),
            param(QStringLiteral("format"), QStringLiteral("string"), false,
                QStringLiteral("One of PNG/JPG/BMP/TIF/PPM (case-insensitive). Derived from \"path\"'s file extension if omitted; falls back to PNG if the extension is unrecognized/absent.")),
            param(QStringLiteral("background"), QStringLiteral("string"), false,
                QStringLiteral("\"transparent\", \"white\", or a QColor-parsable string (e.g. \"#RRGGBB\"). Default depends on format: transparent for PNG/TIF/PPM, white for JPG/BMP.")),
            param(QStringLiteral("padding"), QStringLiteral("number"), false,
                QStringLiteral("Literal margin (scene units) added around the tight content bounding box on every side."), QStringLiteral("20")),
            param(QStringLiteral("width"), QStringLiteral("number"), false,
                QStringLiteral("Literal explicit pixel width. Given with \"height\": used verbatim (may distort aspect ratio, by design). Given alone: height is derived from the content's aspect ratio.")),
            param(QStringLiteral("height"), QStringLiteral("number"), false,
                QStringLiteral("Literal explicit pixel height. See \"width\". Neither given: renders 1:1 scene-unit-to-pixel, capped at 4096px on the larger dimension.")),
            param(QStringLiteral("highlight"), QStringLiteral("array"), false,
                QStringLiteral("Object names to overlay with a semi-transparent red rectangle. A name that fails to resolve, or has no live graphics item, is silently added to the response's \"skippedHighlights\" rather than failing the whole render."),
                QString(), QStringLiteral("string")),
            param(QStringLiteral("showPointNames"), QStringLiteral("boolean"), false,
                QStringLiteral("Literal flag: force point-name labels (e.g. \"A1\", \"A2\") on (true) or off (false) for this render, overriding the pattern's own scene-wide setting for the duration of this one call only. A point created with its own \"showPointName\": false (see basePoint/endLine/etc.) still never shows its label even when this is true -- this only controls the scene-wide toggle, not any individual point's own flag. Omit to leave the scene-wide setting exactly as the pattern/session already has it.")),
        },
        QStringLiteral(R"({ "op": "render.snapshot", "path": "square.png", "width": 400, "height": 400, "highlight": ["A", "B"], "showPointNames": true })")));

    registerAction(QStringLiteral("export.scene"), &handleExportScene, buildSchema(
        QStringLiteral("export.scene"), QStringLiteral("introspection"),
        QStringLiteral("Writes the current draft scene, as-is (no piece nesting/cutting-layout arrangement -- see piece.* ops and docs/export-actions-notes.md for that out-of-scope Phase B work), to a vector, flat-DXF, or raster file. Read-only with respect to pattern data; only writes the output file itself."),
        {
            param(QStringLiteral("path"), QStringLiteral("string"), true,
                QStringLiteral("Output file path. Parent directory created if missing.")),
            param(QStringLiteral("format"), QStringLiteral("string"), false,
                QStringLiteral("One of svg, pdf, ps, eps, png, jpg, bmp, ppm, tif, dxf-r10, dxf-r12, dxf-r13, dxf-r14, dxf-2000, dxf-2004, dxf-2007, dxf-2010, dxf-2013 (case-insensitive). Derived from \"path\"'s file extension if omitted, except a bare \".dxf\" extension -- which spans nine AutoCAD versions -- requires an explicit \"format\"; an unrecognized/absent extension falls back to png.")),
            param(QStringLiteral("target"), QStringLiteral("string"), false,
                QStringLiteral("Which scene to export. \"draft\" is the only value accepted today -- ActionContext exposes no piece scene to this op yet; anything else is a hard error, not a silent fallback."),
                QStringLiteral("draft")),
            param(QStringLiteral("padding"), QStringLiteral("number"), false,
                QStringLiteral("Literal margin (scene units) added around the tight content bounding box on every side."), QStringLiteral("20")),
            param(QStringLiteral("width"), QStringLiteral("number"), false,
                QStringLiteral("Literal explicit device width (pixels for raster formats; scene units treated as the output's own unit for every vector/DXF format). Given with \"height\": used verbatim (may distort aspect ratio, by design). Given alone: the other dimension is derived from the content's aspect ratio.")),
            param(QStringLiteral("height"), QStringLiteral("number"), false,
                QStringLiteral("Literal explicit device height. See \"width\". Neither given: renders 1:1 scene-unit-to-device-unit, capped at 4096 on the larger dimension for raster formats only (a vector/DXF device never allocates a pixel buffer, so it is never capped).")),
            param(QStringLiteral("background"), QStringLiteral("string"), false,
                QStringLiteral("Raster formats (png/jpg/bmp/ppm/tif) only; silently unused for every other format. \"transparent\", \"white\", or a QColor-parsable string (e.g. \"#RRGGBB\"). Default depends on format: transparent for png/ppm/tif, white for jpg/bmp.")),
            param(QStringLiteral("binaryDXF"), QStringLiteral("boolean"), false,
                QStringLiteral("Literal flag: write a binary DXF file instead of ASCII. Only meaningful for the nine dxf-* formats; silently unused otherwise."), QStringLiteral("false")),
            param(QStringLiteral("showPointNames"), QStringLiteral("boolean"), false,
                QStringLiteral("Literal flag: force point-name labels (e.g. \"A1\", \"A2\") on (true) or off (false) for this export, overriding the pattern's own scene-wide setting for the duration of this one call only. Omit to leave the scene-wide setting exactly as the pattern/session already has it.")),
        },
        QStringLiteral(R"({ "op": "export.scene", "path": "square.svg" })"),
        /*partial=*/true,
        QStringLiteral("ps/eps export shells out to the external \"pdftops\" tool (from Poppler/Xpdf); it fails cleanly with a specific error if that binary is not on PATH, rather than producing output.")));

    registerAction(QStringLiteral("pattern.resolveName"), &handlePatternResolveName, buildSchema(
        QStringLiteral("pattern.resolveName"), QStringLiteral("introspection"),
        QStringLiteral("Resolves a name to its internal id and geometry type. Deliberately unscoped (unlike every mutating handler): matches a name in any Draw mode, including a piece-node clone, so it can diagnose an otherwise-invisible name collision."),
        {
            param(QStringLiteral("name"), QStringLiteral("string"), true,
                QStringLiteral("The user-visible name to resolve.")),
        },
        QStringLiteral(R"({ "op": "pattern.resolveName", "name": "A1" })")));

    // ---- Points --------------------------------------------------------------------------------
    registerAction(QStringLiteral("basePoint"), &handleBasePoint, buildSchema(
        QStringLiteral("basePoint"), QStringLiteral("point"),
        QStringLiteral("Creates a point at literal coordinates and starts a brand-new draft block anchored at it. \"draftBlock\" must not already exist -- this op only ever starts a new block, it never adds into an existing one."),
        {
            param(QStringLiteral("name"), QStringLiteral("string"), true, QStringLiteral("The new point's name.")),
            param(QStringLiteral("x"), QStringLiteral("number"), true,
                QStringLiteral("Literal X coordinate, in \"unit\" (default mm). Not a formula.")),
            param(QStringLiteral("y"), QStringLiteral("number"), true,
                QStringLiteral("Literal Y coordinate, in \"unit\" (default mm). Not a formula.")),
            param(QStringLiteral("draftBlock"), QStringLiteral("string"), true,
                QStringLiteral("Name of the new draft block this point anchors. Must NOT already exist.")),
            param(QStringLiteral("mx"), QStringLiteral("number"), false, QStringLiteral("Literal label x-offset."), QStringLiteral("0")),
            param(QStringLiteral("my"), QStringLiteral("number"), false, QStringLiteral("Literal label y-offset."), QStringLiteral("0")),
            param(QStringLiteral("unit"), QStringLiteral("string"), false,
                QStringLiteral("Unit \"x\"/\"y\" are expressed in (\"mm\", \"cm\", \"inch\", ...)."), QStringLiteral("mm")),
        },
        QStringLiteral(R"({ "op": "basePoint", "name": "A", "x": 0, "y": 0, "draftBlock": "Front" })")));

    registerAction(QStringLiteral("line"), &handleLine, buildSchema(
        QStringLiteral("line"), QStringLiteral("point"),
        QStringLiteral("Draws a straight line between two existing points."),
        {
            param(QStringLiteral("firstPoint"), QStringLiteral("string"), true, QStringLiteral("Name of the line's first endpoint.")),
            param(QStringLiteral("secondPoint"), QStringLiteral("string"), true, QStringLiteral("Name of the line's second endpoint.")),
            param(QStringLiteral("lineType"), QStringLiteral("string"), false, QStringLiteral("Visual line style."), QStringLiteral("solidLine")),
            param(QStringLiteral("lineWeight"), QStringLiteral("string"), false, QStringLiteral("Visual line weight."), QStringLiteral("0.35")),
            param(QStringLiteral("lineColor"), QStringLiteral("string"), false, QStringLiteral("Visual line color."), QStringLiteral("black")),
        },
        QStringLiteral(R"({ "op": "line", "firstPoint": "A", "secondPoint": "B" })")));

    registerAction(QStringLiteral("point.edit"), &handlePointEdit, buildSchema(
        QStringLiteral("point.edit"), QStringLiteral("point"),
        QStringLiteral("Mutates an existing point's tool in place (coordinates or formula), instead of creating a new point. At least one of \"x\"/\"y\" (together), \"length\", or \"angle\" is required; which fields apply depends on the point's own tool type (see each field's description)."),
        {
            param(QStringLiteral("name"), QStringLiteral("string"), true, QStringLiteral("Name of any existing point.")),
            param(QStringLiteral("x"), QStringLiteral("number"), false,
                QStringLiteral("Literal new X coordinate. Only applies to a basePoint-created point; must be given together with \"y\", never alone.")),
            param(QStringLiteral("y"), QStringLiteral("number"), false,
                QStringLiteral("Literal new Y coordinate. See \"x\".")),
            param(QStringLiteral("length"), QStringLiteral("string"), false,
                QStringLiteral("Formula string evaluated by Seamly2D's formula engine, replacing this point's length formula. Only applies to a VToolLinePoint-family point (endLine/alongLine/normal/bisector/shoulderPoint). A zero-length formula is rejected.")),
            param(QStringLiteral("angle"), QStringLiteral("string"), false,
                QStringLiteral("Formula string evaluated by Seamly2D's formula engine, replacing this point's angle formula. Only applies to an endLine-created point today.")),
        },
        QStringLiteral(R"({ "op": "point.edit", "name": "D", "length": "0" })")));

    // ---- Formula-bearing points ------------------------------------------------------------------
    registerAction(QStringLiteral("endLine"), &handleEndLine, buildSchema(
        QStringLiteral("endLine"), QStringLiteral("formula-point"),
        QStringLiteral("Creates a point at a given formula length and angle from an existing base point."),
        {
            param(QStringLiteral("name"), QStringLiteral("string"), true, QStringLiteral("Name to assign the new point.")),
            param(QStringLiteral("basePoint"), QStringLiteral("string"), true, QStringLiteral("Name of the existing point to measure from.")),
            param(QStringLiteral("length"), QStringLiteral("string"), true,
                QStringLiteral("Formula string for distance from \"basePoint\" (e.g. '10' or 'neck_width/2'), evaluated by Seamly2D's formula engine.")),
            param(QStringLiteral("angle"), QStringLiteral("string"), false,
                QStringLiteral("Formula string for the angle in degrees from \"basePoint\", evaluated by Seamly2D's formula engine."), QStringLiteral("0")),
            param(QStringLiteral("lineType"), QStringLiteral("string"), false, QStringLiteral("Visual guide line style."), QStringLiteral("solidLine")),
            param(QStringLiteral("lineWeight"), QStringLiteral("string"), false, QStringLiteral("Visual guide line weight."), QStringLiteral("0.35")),
            param(QStringLiteral("lineColor"), QStringLiteral("string"), false, QStringLiteral("Visual guide line color."), QStringLiteral("black")),
            param(QStringLiteral("mx"), QStringLiteral("number"), false, QStringLiteral("Literal label x-offset."), QStringLiteral("0")),
            param(QStringLiteral("my"), QStringLiteral("number"), false, QStringLiteral("Literal label y-offset."), QStringLiteral("0")),
            param(QStringLiteral("showPointName"), QStringLiteral("boolean"), false, QStringLiteral("Literal flag: show the point's name label on the draft."), QStringLiteral("true")),
        },
        QStringLiteral(R"({ "op": "endLine", "name": "B", "basePoint": "A", "length": "100", "angle": "0" })")));

    registerAction(QStringLiteral("alongLine"), &handleAlongLine, buildSchema(
        QStringLiteral("alongLine"), QStringLiteral("formula-point"),
        QStringLiteral("Creates a point at a given formula distance from firstPoint, along the line toward secondPoint."),
        {
            param(QStringLiteral("name"), QStringLiteral("string"), true, QStringLiteral("Name to assign the new point.")),
            param(QStringLiteral("firstPoint"), QStringLiteral("string"), true, QStringLiteral("Name of the point the distance is measured from.")),
            param(QStringLiteral("secondPoint"), QStringLiteral("string"), true, QStringLiteral("Name of the point the line points toward.")),
            param(QStringLiteral("length"), QStringLiteral("string"), true,
                QStringLiteral("Formula string for distance from \"firstPoint\" along the line toward \"secondPoint\", evaluated by Seamly2D's formula engine.")),
            param(QStringLiteral("lineType"), QStringLiteral("string"), false, QStringLiteral("Visual guide line style."), QStringLiteral("solidLine")),
            param(QStringLiteral("lineWeight"), QStringLiteral("string"), false, QStringLiteral("Visual guide line weight."), QStringLiteral("0.35")),
            param(QStringLiteral("lineColor"), QStringLiteral("string"), false, QStringLiteral("Visual guide line color."), QStringLiteral("black")),
            param(QStringLiteral("mx"), QStringLiteral("number"), false, QStringLiteral("Literal label x-offset."), QStringLiteral("0")),
            param(QStringLiteral("my"), QStringLiteral("number"), false, QStringLiteral("Literal label y-offset."), QStringLiteral("0")),
            param(QStringLiteral("showPointName"), QStringLiteral("boolean"), false, QStringLiteral("Literal flag: show the point's name label on the draft."), QStringLiteral("true")),
        },
        QStringLiteral(R"({ "op": "alongLine", "name": "M", "firstPoint": "A", "secondPoint": "B", "length": "50" })")));

    registerAction(QStringLiteral("normal"), &handleNormal, buildSchema(
        QStringLiteral("normal"), QStringLiteral("formula-point"),
        QStringLiteral("Creates a point at a given formula distance along the normal (perpendicular) to the line from firstPoint to secondPoint."),
        {
            param(QStringLiteral("name"), QStringLiteral("string"), true, QStringLiteral("Name to assign the new point.")),
            param(QStringLiteral("firstPoint"), QStringLiteral("string"), true, QStringLiteral("First point defining the base line.")),
            param(QStringLiteral("secondPoint"), QStringLiteral("string"), true, QStringLiteral("Second point defining the base line.")),
            param(QStringLiteral("length"), QStringLiteral("string"), true,
                QStringLiteral("Formula string for distance along the normal to the firstPoint-secondPoint line, evaluated by Seamly2D's formula engine.")),
            param(QStringLiteral("angle"), QStringLiteral("number"), false,
                QStringLiteral("Literal rotation offset in degrees (a plain number or numeric string). Unlike every sibling op's \"angle\", this is never evaluated as a formula."), QStringLiteral("0")),
            param(QStringLiteral("lineType"), QStringLiteral("string"), false, QStringLiteral("Visual guide line style."), QStringLiteral("solidLine")),
            param(QStringLiteral("lineWeight"), QStringLiteral("string"), false, QStringLiteral("Visual guide line weight."), QStringLiteral("0.35")),
            param(QStringLiteral("lineColor"), QStringLiteral("string"), false, QStringLiteral("Visual guide line color."), QStringLiteral("black")),
            param(QStringLiteral("mx"), QStringLiteral("number"), false, QStringLiteral("Literal label x-offset."), QStringLiteral("0")),
            param(QStringLiteral("my"), QStringLiteral("number"), false, QStringLiteral("Literal label y-offset."), QStringLiteral("0")),
            param(QStringLiteral("showPointName"), QStringLiteral("boolean"), false, QStringLiteral("Literal flag: show the point's name label on the draft."), QStringLiteral("true")),
        },
        QStringLiteral(R"({ "op": "normal", "name": "N", "firstPoint": "A", "secondPoint": "B", "length": "20", "angle": 0 })")));

    registerAction(QStringLiteral("bisector"), &handleBisector, buildSchema(
        QStringLiteral("bisector"), QStringLiteral("formula-point"),
        QStringLiteral("Creates a point at a given formula distance along the bisector of the angle at secondPoint, between rays to firstPoint and thirdPoint."),
        {
            param(QStringLiteral("name"), QStringLiteral("string"), true, QStringLiteral("Name to assign the new point.")),
            param(QStringLiteral("firstPoint"), QStringLiteral("string"), true, QStringLiteral("First ray's endpoint.")),
            param(QStringLiteral("secondPoint"), QStringLiteral("string"), true, QStringLiteral("Vertex of the angle being bisected.")),
            param(QStringLiteral("thirdPoint"), QStringLiteral("string"), true, QStringLiteral("Second ray's endpoint.")),
            param(QStringLiteral("length"), QStringLiteral("string"), true,
                QStringLiteral("Formula string for distance along the bisector, evaluated by Seamly2D's formula engine.")),
            param(QStringLiteral("lineType"), QStringLiteral("string"), false, QStringLiteral("Visual guide line style."), QStringLiteral("solidLine")),
            param(QStringLiteral("lineWeight"), QStringLiteral("string"), false, QStringLiteral("Visual guide line weight."), QStringLiteral("0.35")),
            param(QStringLiteral("lineColor"), QStringLiteral("string"), false, QStringLiteral("Visual guide line color."), QStringLiteral("black")),
            param(QStringLiteral("mx"), QStringLiteral("number"), false, QStringLiteral("Literal label x-offset."), QStringLiteral("0")),
            param(QStringLiteral("my"), QStringLiteral("number"), false, QStringLiteral("Literal label y-offset."), QStringLiteral("0")),
            param(QStringLiteral("showPointName"), QStringLiteral("boolean"), false, QStringLiteral("Literal flag: show the point's name label on the draft."), QStringLiteral("true")),
        },
        QStringLiteral(R"({ "op": "bisector", "name": "Bi", "firstPoint": "A", "secondPoint": "B", "thirdPoint": "C", "length": "20" })")));

    registerAction(QStringLiteral("shoulderPoint"), &handleShoulderPoint, buildSchema(
        QStringLiteral("shoulderPoint"), QStringLiteral("formula-point"),
        QStringLiteral("Creates a point at a given formula distance (measured from pShoulder) projected onto the line from p1Line to p2Line."),
        {
            param(QStringLiteral("name"), QStringLiteral("string"), true, QStringLiteral("Name to assign the new point.")),
            param(QStringLiteral("p1Line"), QStringLiteral("string"), true, QStringLiteral("First point defining the line the result projects onto.")),
            param(QStringLiteral("p2Line"), QStringLiteral("string"), true, QStringLiteral("Second point defining the line the result projects onto.")),
            param(QStringLiteral("pShoulder"), QStringLiteral("string"), true, QStringLiteral("The \"shoulder\" point the length is measured from.")),
            param(QStringLiteral("length"), QStringLiteral("string"), true,
                QStringLiteral("Formula string for distance from \"pShoulder\", evaluated by Seamly2D's formula engine.")),
            param(QStringLiteral("lineType"), QStringLiteral("string"), false, QStringLiteral("Visual guide line style."), QStringLiteral("solidLine")),
            param(QStringLiteral("lineWeight"), QStringLiteral("string"), false, QStringLiteral("Visual guide line weight."), QStringLiteral("0.35")),
            param(QStringLiteral("lineColor"), QStringLiteral("string"), false, QStringLiteral("Visual guide line color."), QStringLiteral("black")),
            param(QStringLiteral("mx"), QStringLiteral("number"), false, QStringLiteral("Literal label x-offset."), QStringLiteral("0")),
            param(QStringLiteral("my"), QStringLiteral("number"), false, QStringLiteral("Literal label y-offset."), QStringLiteral("0")),
            param(QStringLiteral("showPointName"), QStringLiteral("boolean"), false, QStringLiteral("Literal flag: show the point's name label on the draft."), QStringLiteral("true")),
        },
        QStringLiteral(R"({ "op": "shoulderPoint", "name": "Sh", "p1Line": "A", "p2Line": "B", "pShoulder": "C", "length": "20" })")));

    registerAction(QStringLiteral("lineIntersect"), &handleLineIntersect, buildSchema(
        QStringLiteral("lineIntersect"), QStringLiteral("formula-point"),
        QStringLiteral("Creates a point at the geometric intersection of two lines (each defined by two existing points). Fails if the lines are exactly parallel. No formula involved -- pure geometry."),
        {
            param(QStringLiteral("name"), QStringLiteral("string"), true, QStringLiteral("Name to assign the new point.")),
            param(QStringLiteral("p1Line1"), QStringLiteral("string"), true, QStringLiteral("First line's first endpoint.")),
            param(QStringLiteral("p2Line1"), QStringLiteral("string"), true, QStringLiteral("First line's second endpoint.")),
            param(QStringLiteral("p1Line2"), QStringLiteral("string"), true, QStringLiteral("Second line's first endpoint.")),
            param(QStringLiteral("p2Line2"), QStringLiteral("string"), true, QStringLiteral("Second line's second endpoint.")),
            param(QStringLiteral("mx"), QStringLiteral("number"), false, QStringLiteral("Literal label x-offset."), QStringLiteral("0")),
            param(QStringLiteral("my"), QStringLiteral("number"), false, QStringLiteral("Literal label y-offset."), QStringLiteral("0")),
            param(QStringLiteral("showPointName"), QStringLiteral("boolean"), false, QStringLiteral("Literal flag: show the point's name label on the draft."), QStringLiteral("true")),
        },
        QStringLiteral(R"({ "op": "lineIntersect", "name": "X", "p1Line1": "A", "p2Line1": "B", "p1Line2": "C", "p2Line2": "D" })")));

    // ---- Curves --------------------------------------------------------------------------------
    registerAction(QStringLiteral("spline"), &handleSpline, buildSchema(
        QStringLiteral("spline"), QStringLiteral("curve"),
        QStringLiteral("Draws a quadratic-handle spline curve between point1 and point4, with independent tangent-handle length/angle formulas at each end."),
        {
            param(QStringLiteral("point1"), QStringLiteral("string"), true, QStringLiteral("Curve's first endpoint.")),
            param(QStringLiteral("point4"), QStringLiteral("string"), true, QStringLiteral("Curve's second endpoint.")),
            param(QStringLiteral("length1"), QStringLiteral("string"), true,
                QStringLiteral("Formula string for the tangent-handle length at point1, evaluated by Seamly2D's formula engine.")),
            param(QStringLiteral("length2"), QStringLiteral("string"), true,
                QStringLiteral("Formula string for the tangent-handle length at point4, evaluated by Seamly2D's formula engine.")),
            param(QStringLiteral("angle1"), QStringLiteral("string"), false,
                QStringLiteral("Formula string for the tangent angle at point1, evaluated by Seamly2D's formula engine."), QStringLiteral("0")),
            param(QStringLiteral("angle2"), QStringLiteral("string"), false,
                QStringLiteral("Formula string for the tangent angle at point4, evaluated by Seamly2D's formula engine."), QStringLiteral("0")),
            param(QStringLiteral("duplicate"), QStringLiteral("number"), false, QStringLiteral("Literal duplicate-numbering index."), QStringLiteral("0")),
            param(QStringLiteral("lineColor"), QStringLiteral("string"), false, QStringLiteral("Visual curve color.")),
            param(QStringLiteral("lineType"), QStringLiteral("string"), false, QStringLiteral("Visual curve style (maps onto the underlying penStyle parameter).")),
            param(QStringLiteral("lineWeight"), QStringLiteral("string"), false, QStringLiteral("Visual curve weight.")),
            param(QStringLiteral("autoSmooth"), QStringLiteral("boolean"), false, QStringLiteral("Literal flag: keep tangent handles automatically smoothed."), QStringLiteral("false")),
            param(QStringLiteral("lengthMode"), QStringLiteral("number"), false, QStringLiteral("Literal handle-length mode index."), QStringLiteral("0")),
            param(QStringLiteral("targetLength"), QStringLiteral("string"), false, QStringLiteral("Target overall curve length, when lengthMode requires one.")),
        },
        QStringLiteral(R"({ "op": "spline", "point1": "A", "point4": "B", "angle1": "0", "angle2": "180", "length1": "30", "length2": "30" })")));

    registerAction(QStringLiteral("splinePath"), &handleSplinePath, buildSchema(
        QStringLiteral("splinePath"), QStringLiteral("curve"),
        QStringLiteral("Draws a multi-point spline path through every point in \"points\" (at least two), each with its own tangent-handle formulas."),
        {
            param(QStringLiteral("points"), QStringLiteral("array"), true,
                QStringLiteral("At least two entries, each {\"point\", \"angle1\"?, \"angle2\"?, \"length1\", \"length2\"}. \"angle1\"/\"angle2\" default \"0\" per entry; \"length1\"/\"length2\" (formula strings, evaluated by Seamly2D's formula engine) are required per entry -- one quad of formulas per point, not per segment."),
                QString(), QStringLiteral("object")),
            param(QStringLiteral("duplicate"), QStringLiteral("number"), false, QStringLiteral("Literal, action-level duplicate-numbering index."), QStringLiteral("0")),
            param(QStringLiteral("lineColor"), QStringLiteral("string"), false, QStringLiteral("Visual curve color (action-level, not per-point).")),
            param(QStringLiteral("lineType"), QStringLiteral("string"), false, QStringLiteral("Visual curve style (action-level, not per-point).")),
            param(QStringLiteral("lineWeight"), QStringLiteral("string"), false, QStringLiteral("Visual curve weight (action-level, not per-point).")),
        },
        QStringLiteral(R"({ "op": "splinePath", "points": [ { "point": "A", "angle1": "0", "angle2": "180", "length1": "30", "length2": "30" }, { "point": "B", "angle1": "0", "angle2": "180", "length1": "30", "length2": "30" } ] })")));

    registerAction(QStringLiteral("cubicBezier"), &handleCubicBezier, buildSchema(
        QStringLiteral("cubicBezier"), QStringLiteral("curve"),
        QStringLiteral("Draws a cubic Bezier curve from point1 to point4, using point2/point3 as control points. No formula involved -- shape is defined entirely by the four named points."),
        {
            param(QStringLiteral("point1"), QStringLiteral("string"), true, QStringLiteral("Curve's first endpoint.")),
            param(QStringLiteral("point2"), QStringLiteral("string"), true, QStringLiteral("First control point.")),
            param(QStringLiteral("point3"), QStringLiteral("string"), true, QStringLiteral("Second control point.")),
            param(QStringLiteral("point4"), QStringLiteral("string"), true, QStringLiteral("Curve's second endpoint.")),
            param(QStringLiteral("lineColor"), QStringLiteral("string"), false, QStringLiteral("Visual curve color.")),
            param(QStringLiteral("lineType"), QStringLiteral("string"), false, QStringLiteral("Visual curve style.")),
            param(QStringLiteral("lineWeight"), QStringLiteral("string"), false, QStringLiteral("Visual curve weight.")),
        },
        QStringLiteral(R"({ "op": "cubicBezier", "point1": "A", "point2": "CP1", "point3": "CP2", "point4": "B" })")));

    registerAction(QStringLiteral("cubicBezierPath"), &handleCubicBezierPath, buildSchema(
        QStringLiteral("cubicBezierPath"), QStringLiteral("curve"),
        QStringLiteral("Draws a multi-segment cubic Bezier path through a flat list of already-named points. No formula involved."),
        {
            param(QStringLiteral("points"), QStringLiteral("array"), true,
                QStringLiteral("Flat list of point names: 4 entries for one segment, then 3 more per additional segment ((n-4) % 3 == 0). Each extra segment reuses the previous segment's last point as its own start."),
                QString(), QStringLiteral("string")),
            param(QStringLiteral("lineColor"), QStringLiteral("string"), false, QStringLiteral("Visual curve color.")),
            param(QStringLiteral("lineType"), QStringLiteral("string"), false, QStringLiteral("Visual curve style.")),
            param(QStringLiteral("lineWeight"), QStringLiteral("string"), false, QStringLiteral("Visual curve weight.")),
        },
        QStringLiteral(R"({ "op": "cubicBezierPath", "points": ["A", "CP1", "CP2", "B", "CP3", "CP4", "C"] })")));

    registerAction(QStringLiteral("arc"), &handleArc, buildSchema(
        QStringLiteral("arc"), QStringLiteral("curve"),
        QStringLiteral("Draws a circular arc of a given formula radius around center, from formula start angle f1 to formula end angle f2."),
        {
            param(QStringLiteral("center"), QStringLiteral("string"), true, QStringLiteral("Arc's center point.")),
            param(QStringLiteral("radius"), QStringLiteral("string"), true,
                QStringLiteral("Formula string for the arc's radius, evaluated by Seamly2D's formula engine.")),
            param(QStringLiteral("f1"), QStringLiteral("string"), true,
                QStringLiteral("Formula string for the start angle in degrees, evaluated by Seamly2D's formula engine.")),
            param(QStringLiteral("f2"), QStringLiteral("string"), true,
                QStringLiteral("Formula string for the end angle in degrees, evaluated by Seamly2D's formula engine.")),
            param(QStringLiteral("lineColor"), QStringLiteral("string"), false, QStringLiteral("Visual curve color.")),
            param(QStringLiteral("lineType"), QStringLiteral("string"), false, QStringLiteral("Visual curve style.")),
            param(QStringLiteral("lineWeight"), QStringLiteral("string"), false, QStringLiteral("Visual curve weight.")),
        },
        QStringLiteral(R"({ "op": "arc", "center": "A", "radius": "50", "f1": "0", "f2": "90" })")));

    registerAction(QStringLiteral("arcWithLength"), &handleArcWithLength, buildSchema(
        QStringLiteral("arcWithLength"), QStringLiteral("curve"),
        QStringLiteral("Draws a circular arc of a given formula radius around center, starting at formula angle f1 and sweeping a formula arc length (instead of a second angle)."),
        {
            param(QStringLiteral("center"), QStringLiteral("string"), true, QStringLiteral("Arc's center point.")),
            param(QStringLiteral("radius"), QStringLiteral("string"), true,
                QStringLiteral("Formula string for the arc's radius, evaluated by Seamly2D's formula engine.")),
            param(QStringLiteral("f1"), QStringLiteral("string"), true,
                QStringLiteral("Formula string for the start angle in degrees, evaluated by Seamly2D's formula engine.")),
            param(QStringLiteral("length"), QStringLiteral("string"), true,
                QStringLiteral("Formula string for the arc's length (instead of a second angle), evaluated by Seamly2D's formula engine.")),
            param(QStringLiteral("lineColor"), QStringLiteral("string"), false, QStringLiteral("Visual curve color.")),
            param(QStringLiteral("lineType"), QStringLiteral("string"), false, QStringLiteral("Visual curve style.")),
            param(QStringLiteral("lineWeight"), QStringLiteral("string"), false, QStringLiteral("Visual curve weight.")),
        },
        QStringLiteral(R"({ "op": "arcWithLength", "center": "A", "radius": "50", "f1": "0", "length": "78.5" })")));

    registerAction(QStringLiteral("ellipticalArc"), &handleEllipticalArc, buildSchema(
        QStringLiteral("ellipticalArc"), QStringLiteral("curve"),
        QStringLiteral("Draws an elliptical arc around center with independent formula radii on each axis, from formula start angle f1 to formula end angle f2, with an optional formula rotation."),
        {
            param(QStringLiteral("center"), QStringLiteral("string"), true, QStringLiteral("Arc's center point.")),
            param(QStringLiteral("radius1"), QStringLiteral("string"), true,
                QStringLiteral("Formula string for the first (typically horizontal) radius, evaluated by Seamly2D's formula engine.")),
            param(QStringLiteral("radius2"), QStringLiteral("string"), true,
                QStringLiteral("Formula string for the second (typically vertical) radius, evaluated by Seamly2D's formula engine.")),
            param(QStringLiteral("f1"), QStringLiteral("string"), true,
                QStringLiteral("Formula string for the start angle in degrees, evaluated by Seamly2D's formula engine.")),
            param(QStringLiteral("f2"), QStringLiteral("string"), true,
                QStringLiteral("Formula string for the end angle in degrees, evaluated by Seamly2D's formula engine.")),
            param(QStringLiteral("rotationAngle"), QStringLiteral("string"), false,
                QStringLiteral("Formula string for the ellipse's own rotation in degrees, evaluated by Seamly2D's formula engine."), QStringLiteral("0")),
            param(QStringLiteral("lineColor"), QStringLiteral("string"), false, QStringLiteral("Visual curve color.")),
            param(QStringLiteral("lineType"), QStringLiteral("string"), false, QStringLiteral("Visual curve style.")),
            param(QStringLiteral("lineWeight"), QStringLiteral("string"), false, QStringLiteral("Visual curve weight.")),
        },
        QStringLiteral(R"({ "op": "ellipticalArc", "center": "A", "radius1": "60", "radius2": "30", "f1": "0", "f2": "180" })")));

    // ---- Cut / intersection points ---------------------------------------------------------------
    registerAction(QStringLiteral("cutSpline"), &handleCutSpline, buildSchema(
        QStringLiteral("cutSpline"), QStringLiteral("cut-point"),
        QStringLiteral("Creates a point at a given formula distance from one end of an existing spline/splinePath/cubicBezier/cubicBezierPath curve, cutting it into two segments."),
        {
            param(QStringLiteral("name"), QStringLiteral("string"), true, QStringLiteral("Name to assign the new point.")),
            param(QStringLiteral("curve"), QStringLiteral("string"), true,
                QStringLiteral("Name of an existing spline/splinePath/cubicBezier/cubicBezierPath curve.")),
            param(QStringLiteral("direction"), QStringLiteral("string"), true,
                QStringLiteral("\"forward\" or anything else (treated as \"backward\"); which end of the curve \"length\" is measured from.")),
            param(QStringLiteral("length"), QStringLiteral("string"), true,
                QStringLiteral("Formula string for distance from the chosen end, evaluated by Seamly2D's formula engine.")),
            param(QStringLiteral("lineColor"), QStringLiteral("string"), false, QStringLiteral("Visual marker color.")),
            param(QStringLiteral("mx"), QStringLiteral("number"), false, QStringLiteral("Literal label x-offset."), QStringLiteral("0")),
            param(QStringLiteral("my"), QStringLiteral("number"), false, QStringLiteral("Literal label y-offset."), QStringLiteral("0")),
            param(QStringLiteral("showPointName"), QStringLiteral("boolean"), false, QStringLiteral("Literal flag: show the point's name label on the draft."), QStringLiteral("true")),
        },
        QStringLiteral(R"({ "op": "cutSpline", "name": "Cut1", "curve": "Curve1", "direction": "forward", "length": "50" })")));

    registerAction(QStringLiteral("cutArc"), &handleCutArc, buildSchema(
        QStringLiteral("cutArc"), QStringLiteral("cut-point"),
        QStringLiteral("Creates a point at a given formula distance from one end of an existing arc, cutting it into two segments."),
        {
            param(QStringLiteral("name"), QStringLiteral("string"), true, QStringLiteral("Name to assign the new point.")),
            param(QStringLiteral("arc"), QStringLiteral("string"), true, QStringLiteral("Name of an existing arc.")),
            param(QStringLiteral("direction"), QStringLiteral("string"), true,
                QStringLiteral("\"forward\" or anything else (treated as \"backward\"); which end of the arc \"length\" is measured from.")),
            param(QStringLiteral("length"), QStringLiteral("string"), true,
                QStringLiteral("Formula string for distance from the chosen end, evaluated by Seamly2D's formula engine.")),
            param(QStringLiteral("lineColor"), QStringLiteral("string"), false, QStringLiteral("Visual marker color.")),
            param(QStringLiteral("mx"), QStringLiteral("number"), false, QStringLiteral("Literal label x-offset."), QStringLiteral("0")),
            param(QStringLiteral("my"), QStringLiteral("number"), false, QStringLiteral("Literal label y-offset."), QStringLiteral("0")),
            param(QStringLiteral("showPointName"), QStringLiteral("boolean"), false, QStringLiteral("Literal flag: show the point's name label on the draft."), QStringLiteral("true")),
        },
        QStringLiteral(R"({ "op": "cutArc", "name": "Cut2", "arc": "Arc1", "direction": "forward", "length": "30" })")));

    registerAction(QStringLiteral("pointOfIntersectionArcs"), &handlePointOfIntersectionArcs, buildSchema(
        QStringLiteral("pointOfIntersectionArcs"), QStringLiteral("cut-point"),
        QStringLiteral("Creates a point at one of the (up to two) intersections of two existing arcs. No formula involved -- pure geometry."),
        {
            param(QStringLiteral("name"), QStringLiteral("string"), true, QStringLiteral("Name to assign the new point.")),
            param(QStringLiteral("firstArc"), QStringLiteral("string"), true, QStringLiteral("Name of the first arc.")),
            param(QStringLiteral("secondArc"), QStringLiteral("string"), true, QStringLiteral("Name of the second arc.")),
            param(QStringLiteral("crossPoint"), QStringLiteral("string"), true,
                QStringLiteral("\"firstPoint\" or \"secondPoint\" (case-insensitive): which of the two intersections to use.")),
            param(QStringLiteral("mx"), QStringLiteral("number"), false, QStringLiteral("Literal label x-offset."), QStringLiteral("0")),
            param(QStringLiteral("my"), QStringLiteral("number"), false, QStringLiteral("Literal label y-offset."), QStringLiteral("0")),
        },
        QStringLiteral(R"({ "op": "pointOfIntersectionArcs", "name": "X1", "firstArc": "Arc1", "secondArc": "Arc2", "crossPoint": "firstPoint" })")));

    registerAction(QStringLiteral("pointOfIntersectionCircles"), &handlePointOfIntersectionCircles, buildSchema(
        QStringLiteral("pointOfIntersectionCircles"), QStringLiteral("cut-point"),
        QStringLiteral("Creates a point at one of the (up to two) intersections of two circles, each defined by a center point and a formula radius."),
        {
            param(QStringLiteral("name"), QStringLiteral("string"), true, QStringLiteral("Name to assign the new point.")),
            param(QStringLiteral("firstCircleCenter"), QStringLiteral("string"), true, QStringLiteral("Center point of the first circle.")),
            param(QStringLiteral("secondCircleCenter"), QStringLiteral("string"), true, QStringLiteral("Center point of the second circle.")),
            param(QStringLiteral("firstCircleRadius"), QStringLiteral("string"), true,
                QStringLiteral("Formula string for the first circle's radius, evaluated by Seamly2D's formula engine.")),
            param(QStringLiteral("secondCircleRadius"), QStringLiteral("string"), true,
                QStringLiteral("Formula string for the second circle's radius, evaluated by Seamly2D's formula engine.")),
            param(QStringLiteral("crossPoint"), QStringLiteral("string"), true,
                QStringLiteral("\"firstPoint\" or \"secondPoint\" (case-insensitive): which of the two intersections to use.")),
            param(QStringLiteral("mx"), QStringLiteral("number"), false, QStringLiteral("Literal label x-offset."), QStringLiteral("0")),
            param(QStringLiteral("my"), QStringLiteral("number"), false, QStringLiteral("Literal label y-offset."), QStringLiteral("0")),
            param(QStringLiteral("showPointName"), QStringLiteral("boolean"), false, QStringLiteral("Literal flag: show the point's name label on the draft."), QStringLiteral("true")),
        },
        QStringLiteral(R"({ "op": "pointOfIntersectionCircles", "name": "X2", "firstCircleCenter": "A", "secondCircleCenter": "B", "firstCircleRadius": "50", "secondCircleRadius": "60", "crossPoint": "firstPoint" })")));

    registerAction(QStringLiteral("pointOfIntersectionCurves"), &handlePointOfIntersectionCurves, buildSchema(
        QStringLiteral("pointOfIntersectionCurves"), QStringLiteral("cut-point"),
        QStringLiteral("Creates a point at the intersection of two existing curves (arc, elliptical arc, spline, splinePath, cubicBezier, or cubicBezierPath), disambiguated by vertical/horizontal extremes. No formula involved."),
        {
            param(QStringLiteral("name"), QStringLiteral("string"), true, QStringLiteral("Name to assign the new point.")),
            param(QStringLiteral("firstCurve"), QStringLiteral("string"), true, QStringLiteral("Name of the first curve.")),
            param(QStringLiteral("secondCurve"), QStringLiteral("string"), true, QStringLiteral("Name of the second curve.")),
            param(QStringLiteral("vCrossPoint"), QStringLiteral("string"), true,
                QStringLiteral("\"highestPoint\" or \"lowestPoint\" (case-insensitive).")),
            param(QStringLiteral("hCrossPoint"), QStringLiteral("string"), true,
                QStringLiteral("\"leftmostPoint\" or \"rightmostPoint\" (case-insensitive).")),
            param(QStringLiteral("mx"), QStringLiteral("number"), false, QStringLiteral("Literal label x-offset."), QStringLiteral("0")),
            param(QStringLiteral("my"), QStringLiteral("number"), false, QStringLiteral("Literal label y-offset."), QStringLiteral("0")),
            param(QStringLiteral("showPointName"), QStringLiteral("boolean"), false, QStringLiteral("Literal flag: show the point's name label on the draft."), QStringLiteral("true")),
        },
        QStringLiteral(R"({ "op": "pointOfIntersectionCurves", "name": "X3", "firstCurve": "Curve1", "secondCurve": "Curve2", "vCrossPoint": "highestPoint", "hCrossPoint": "leftmostPoint" })")));

    registerAction(QStringLiteral("curveIntersectAxis"), &handleCurveIntersectAxis, buildSchema(
        QStringLiteral("curveIntersectAxis"), QStringLiteral("cut-point"),
        QStringLiteral("Creates a point where a ray from basePoint at a given formula angle intersects an existing curve (any curve type, including an arc)."),
        {
            param(QStringLiteral("name"), QStringLiteral("string"), true, QStringLiteral("Name to assign the new point.")),
            param(QStringLiteral("basePoint"), QStringLiteral("string"), true, QStringLiteral("Origin point the ray is cast from.")),
            param(QStringLiteral("curve"), QStringLiteral("string"), true, QStringLiteral("Name of the curve the ray is intersected against.")),
            param(QStringLiteral("angle"), QStringLiteral("string"), true,
                QStringLiteral("Formula string for the ray's angle in degrees from \"basePoint\", evaluated by Seamly2D's formula engine.")),
            param(QStringLiteral("lineType"), QStringLiteral("string"), false, QStringLiteral("Visual guide line style."), QStringLiteral("solidLine")),
            param(QStringLiteral("lineWeight"), QStringLiteral("string"), false, QStringLiteral("Visual guide line weight."), QStringLiteral("0.35")),
            param(QStringLiteral("lineColor"), QStringLiteral("string"), false, QStringLiteral("Visual guide line color."), QStringLiteral("black")),
            param(QStringLiteral("mx"), QStringLiteral("number"), false, QStringLiteral("Literal label x-offset."), QStringLiteral("0")),
            param(QStringLiteral("my"), QStringLiteral("number"), false, QStringLiteral("Literal label y-offset."), QStringLiteral("0")),
            param(QStringLiteral("showPointName"), QStringLiteral("boolean"), false, QStringLiteral("Literal flag: show the point's name label on the draft."), QStringLiteral("true")),
        },
        QStringLiteral(R"({ "op": "curveIntersectAxis", "name": "X4", "basePoint": "A", "curve": "Curve1", "angle": "45" })")));

    registerAction(QStringLiteral("pointFromCircleAndTangent"), &handlePointFromCircleAndTangent, buildSchema(
        QStringLiteral("pointFromCircleAndTangent"), QStringLiteral("cut-point"),
        QStringLiteral("Creates a point at one of the (up to two) tangent points from an external point to a circle defined by a center point and a formula radius."),
        {
            param(QStringLiteral("name"), QStringLiteral("string"), true, QStringLiteral("Name to assign the new point.")),
            param(QStringLiteral("circleCenter"), QStringLiteral("string"), true, QStringLiteral("Center point of the circle.")),
            param(QStringLiteral("circleRadius"), QStringLiteral("string"), true,
                QStringLiteral("Formula string for the circle's radius, evaluated by Seamly2D's formula engine.")),
            param(QStringLiteral("tangentPoint"), QStringLiteral("string"), true, QStringLiteral("External point the tangent line passes through.")),
            param(QStringLiteral("crossPoint"), QStringLiteral("string"), true,
                QStringLiteral("\"firstPoint\" or \"secondPoint\" (case-insensitive): which of the two tangent points to use.")),
            param(QStringLiteral("mx"), QStringLiteral("number"), false, QStringLiteral("Literal label x-offset."), QStringLiteral("0")),
            param(QStringLiteral("my"), QStringLiteral("number"), false, QStringLiteral("Literal label y-offset."), QStringLiteral("0")),
            param(QStringLiteral("showPointName"), QStringLiteral("boolean"), false, QStringLiteral("Literal flag: show the point's name label on the draft."), QStringLiteral("true")),
        },
        QStringLiteral(R"({ "op": "pointFromCircleAndTangent", "name": "T1", "circleCenter": "A", "circleRadius": "40", "tangentPoint": "B", "crossPoint": "firstPoint" })")));

    registerAction(QStringLiteral("pointFromArcAndTangent"), &handlePointFromArcAndTangent, buildSchema(
        QStringLiteral("pointFromArcAndTangent"), QStringLiteral("cut-point"),
        QStringLiteral("Creates a point at one of the (up to two) tangent points from an external point to an existing arc. No formula involved."),
        {
            param(QStringLiteral("name"), QStringLiteral("string"), true, QStringLiteral("Name to assign the new point.")),
            param(QStringLiteral("arc"), QStringLiteral("string"), true, QStringLiteral("Name of the arc.")),
            param(QStringLiteral("tangentPoint"), QStringLiteral("string"), true, QStringLiteral("External point the tangent line passes through.")),
            param(QStringLiteral("crossPoint"), QStringLiteral("string"), true,
                QStringLiteral("\"firstPoint\" or \"secondPoint\" (case-insensitive): which of the two tangent points to use.")),
            param(QStringLiteral("mx"), QStringLiteral("number"), false, QStringLiteral("Literal label x-offset."), QStringLiteral("0")),
            param(QStringLiteral("my"), QStringLiteral("number"), false, QStringLiteral("Literal label y-offset."), QStringLiteral("0")),
            param(QStringLiteral("showPointName"), QStringLiteral("boolean"), false, QStringLiteral("Literal flag: show the point's name label on the draft."), QStringLiteral("true")),
        },
        QStringLiteral(R"({ "op": "pointFromArcAndTangent", "name": "T2", "arc": "Arc1", "tangentPoint": "B", "crossPoint": "firstPoint" })")));

    registerAction(QStringLiteral("triangle"), &handleTriangle, buildSchema(
        QStringLiteral("triangle"), QStringLiteral("cut-point"),
        QStringLiteral("Creates a point on the axis line (axisP1-axisP2) where a right angle forms with the hypotenuse's two ends (firstPoint, secondPoint). No formula involved."),
        {
            param(QStringLiteral("name"), QStringLiteral("string"), true, QStringLiteral("Name to assign the new point.")),
            param(QStringLiteral("axisP1"), QStringLiteral("string"), true, QStringLiteral("First point defining the axis line the result lies on.")),
            param(QStringLiteral("axisP2"), QStringLiteral("string"), true, QStringLiteral("Second point defining the axis line the result lies on.")),
            param(QStringLiteral("firstPoint"), QStringLiteral("string"), true, QStringLiteral("First end of the hypotenuse.")),
            param(QStringLiteral("secondPoint"), QStringLiteral("string"), true, QStringLiteral("Second end of the hypotenuse.")),
            param(QStringLiteral("mx"), QStringLiteral("number"), false, QStringLiteral("Literal label x-offset."), QStringLiteral("0")),
            param(QStringLiteral("my"), QStringLiteral("number"), false, QStringLiteral("Literal label y-offset."), QStringLiteral("0")),
            param(QStringLiteral("showPointName"), QStringLiteral("boolean"), false, QStringLiteral("Literal flag: show the point's name label on the draft."), QStringLiteral("true")),
        },
        QStringLiteral(R"({ "op": "triangle", "name": "Tr", "axisP1": "A", "axisP2": "B", "firstPoint": "C", "secondPoint": "D" })")));

    registerAction(QStringLiteral("height"), &handleHeight, buildSchema(
        QStringLiteral("height"), QStringLiteral("cut-point"),
        QStringLiteral("Creates a point by projecting basePoint perpendicular onto the line from p1Line to p2Line. No formula involved."),
        {
            param(QStringLiteral("name"), QStringLiteral("string"), true, QStringLiteral("Name to assign the new point.")),
            param(QStringLiteral("basePoint"), QStringLiteral("string"), true, QStringLiteral("Point being projected.")),
            param(QStringLiteral("p1Line"), QStringLiteral("string"), true, QStringLiteral("First point defining the line \"basePoint\" is projected onto.")),
            param(QStringLiteral("p2Line"), QStringLiteral("string"), true, QStringLiteral("Second point defining the line \"basePoint\" is projected onto.")),
            param(QStringLiteral("lineType"), QStringLiteral("string"), false, QStringLiteral("Visual guide line style."), QStringLiteral("solidLine")),
            param(QStringLiteral("lineWeight"), QStringLiteral("string"), false, QStringLiteral("Visual guide line weight."), QStringLiteral("0.35")),
            param(QStringLiteral("lineColor"), QStringLiteral("string"), false, QStringLiteral("Visual guide line color."), QStringLiteral("black")),
            param(QStringLiteral("mx"), QStringLiteral("number"), false, QStringLiteral("Literal label x-offset."), QStringLiteral("0")),
            param(QStringLiteral("my"), QStringLiteral("number"), false, QStringLiteral("Literal label y-offset."), QStringLiteral("0")),
            param(QStringLiteral("showPointName"), QStringLiteral("boolean"), false, QStringLiteral("Literal flag: show the point's name label on the draft."), QStringLiteral("true")),
        },
        QStringLiteral(R"({ "op": "height", "name": "H", "basePoint": "C", "p1Line": "A", "p2Line": "B" })")));

    // ---- Operations ----------------------------------------------------------------------------
    registerAction(QStringLiteral("move"), &handleMove, buildSchema(
        QStringLiteral("move"), QStringLiteral("operation"),
        QStringLiteral("Copies every named source object (any geometry type, not just points) along a formula-driven translate+rotate, naming each copy by appending \"suffix\" to its source name."),
        {
            param(QStringLiteral("sourceObjects"), QStringLiteral("array"), true,
                QStringLiteral("Names of any existing geometry objects (points, lines, arcs, curves, ...) to copy."), QString(), QStringLiteral("string")),
            param(QStringLiteral("suffix"), QStringLiteral("string"), true,
                QStringLiteral("Appended to each source name to name the new object (e.g. \"A1\" + \"_m\" -> \"A1_m\").")),
            param(QStringLiteral("length"), QStringLiteral("string"), true,
                QStringLiteral("Formula string for translation distance, evaluated by Seamly2D's formula engine.")),
            param(QStringLiteral("angle"), QStringLiteral("string"), true,
                QStringLiteral("Formula string for translation direction in degrees, evaluated by Seamly2D's formula engine.")),
            param(QStringLiteral("rotationAngle"), QStringLiteral("string"), true,
                QStringLiteral("Formula string for how much to additionally rotate each copy, evaluated by Seamly2D's formula engine.")),
            param(QStringLiteral("rotationOrigin"), QStringLiteral("string"), false,
                QStringLiteral("Name of the point to rotate around. Omitted: derived from the source objects' own centroid.")),
            param(QStringLiteral("lineType"), QStringLiteral("string"), false, QStringLiteral("Visual line style, applied uniformly to every copied item.")),
            param(QStringLiteral("lineWeight"), QStringLiteral("string"), false, QStringLiteral("Visual line weight, applied uniformly to every copied item.")),
            param(QStringLiteral("lineColor"), QStringLiteral("string"), false, QStringLiteral("Visual line color, applied uniformly to every copied item.")),
        },
        QStringLiteral(R"({ "op": "move", "sourceObjects": ["A", "B"], "suffix": "_m", "length": "20", "angle": "0", "rotationAngle": "0" })")));

    registerAction(QStringLiteral("rotation"), &handleRotation, buildSchema(
        QStringLiteral("rotation"), QStringLiteral("operation"),
        QStringLiteral("Copies every named source object, rotated by a formula angle around an explicit origin point, naming each copy by appending \"suffix\" to its source name."),
        {
            param(QStringLiteral("sourceObjects"), QStringLiteral("array"), true,
                QStringLiteral("Names of any existing geometry objects to copy."), QString(), QStringLiteral("string")),
            param(QStringLiteral("suffix"), QStringLiteral("string"), true,
                QStringLiteral("Appended to each source name to name the new object.")),
            param(QStringLiteral("origin"), QStringLiteral("string"), true,
                QStringLiteral("Point to rotate around. Required here, unlike move's optional \"rotationOrigin\".")),
            param(QStringLiteral("angle"), QStringLiteral("string"), true,
                QStringLiteral("Formula string for the rotation angle in degrees, evaluated by Seamly2D's formula engine.")),
            param(QStringLiteral("lineType"), QStringLiteral("string"), false, QStringLiteral("Visual line style, applied uniformly to every copied item.")),
            param(QStringLiteral("lineWeight"), QStringLiteral("string"), false, QStringLiteral("Visual line weight, applied uniformly to every copied item.")),
            param(QStringLiteral("lineColor"), QStringLiteral("string"), false, QStringLiteral("Visual line color, applied uniformly to every copied item.")),
        },
        QStringLiteral(R"({ "op": "rotation", "sourceObjects": ["A", "B"], "suffix": "_r", "origin": "A", "angle": "90" })")));

    registerAction(QStringLiteral("mirrorByLine"), &handleMirrorByLine, buildSchema(
        QStringLiteral("mirrorByLine"), QStringLiteral("operation"),
        QStringLiteral("Copies every named source object, mirrored across the line defined by firstLinePoint/secondLinePoint, naming each copy by appending \"suffix\" to its source name. No formula involved -- the mirror line is purely geometric."),
        {
            param(QStringLiteral("sourceObjects"), QStringLiteral("array"), true,
                QStringLiteral("Names of any existing geometry objects to copy."), QString(), QStringLiteral("string")),
            param(QStringLiteral("suffix"), QStringLiteral("string"), true,
                QStringLiteral("Appended to each source name to name the new object.")),
            param(QStringLiteral("firstLinePoint"), QStringLiteral("string"), true, QStringLiteral("First point defining the mirror line.")),
            param(QStringLiteral("secondLinePoint"), QStringLiteral("string"), true, QStringLiteral("Second point defining the mirror line.")),
            param(QStringLiteral("lineType"), QStringLiteral("string"), false, QStringLiteral("Visual line style, applied uniformly to every copied item.")),
            param(QStringLiteral("lineWeight"), QStringLiteral("string"), false, QStringLiteral("Visual line weight, applied uniformly to every copied item.")),
            param(QStringLiteral("lineColor"), QStringLiteral("string"), false, QStringLiteral("Visual line color, applied uniformly to every copied item.")),
        },
        QStringLiteral(R"({ "op": "mirrorByLine", "sourceObjects": ["A", "B"], "suffix": "_mir", "firstLinePoint": "A", "secondLinePoint": "B" })")));

    registerAction(QStringLiteral("mirrorByAxis"), &handleMirrorByAxis, buildSchema(
        QStringLiteral("mirrorByAxis"), QStringLiteral("operation"),
        QStringLiteral("Copies every named source object, mirrored across a vertical or horizontal axis through originPoint, naming each copy by appending \"suffix\" to its source name."),
        {
            param(QStringLiteral("sourceObjects"), QStringLiteral("array"), true,
                QStringLiteral("Names of any existing geometry objects to copy."), QString(), QStringLiteral("string")),
            param(QStringLiteral("suffix"), QStringLiteral("string"), true,
                QStringLiteral("Appended to each source name to name the new object.")),
            param(QStringLiteral("originPoint"), QStringLiteral("string"), true, QStringLiteral("Point the mirror axis passes through.")),
            param(QStringLiteral("axisType"), QStringLiteral("string"), true,
                QStringLiteral("\"vertical\" or \"horizontal\" (case-insensitive).")),
            param(QStringLiteral("lineType"), QStringLiteral("string"), false, QStringLiteral("Visual line style, applied uniformly to every copied item.")),
            param(QStringLiteral("lineWeight"), QStringLiteral("string"), false, QStringLiteral("Visual line weight, applied uniformly to every copied item.")),
            param(QStringLiteral("lineColor"), QStringLiteral("string"), false, QStringLiteral("Visual line color, applied uniformly to every copied item.")),
        },
        QStringLiteral(R"({ "op": "mirrorByAxis", "sourceObjects": ["A", "B"], "suffix": "_ax", "originPoint": "A", "axisType": "vertical" })")));

    registerAction(QStringLiteral("group"), &handleGroup, buildSchema(
        QStringLiteral("group"), QStringLiteral("operation"),
        QStringLiteral("Creates a named group containing the listed existing objects, for later show/hide or export bookkeeping. No new geometry is created."),
        {
            param(QStringLiteral("name"), QStringLiteral("string"), true, QStringLiteral("Group name. Must not already exist.")),
            param(QStringLiteral("sourceObjects"), QStringLiteral("array"), true,
                QStringLiteral("Names of existing objects to add to the group."), QString(), QStringLiteral("string")),
            param(QStringLiteral("color"), QStringLiteral("string"), false, QStringLiteral("Visual group color.")),
            param(QStringLiteral("lineType"), QStringLiteral("string"), false, QStringLiteral("Visual group line style.")),
            param(QStringLiteral("lineWeight"), QStringLiteral("string"), false, QStringLiteral("Visual group line weight.")),
        },
        QStringLiteral(R"({ "op": "group", "name": "Notches", "sourceObjects": ["A", "B"] })")));

    registerAction(QStringLiteral("trueDarts"), &handleTrueDarts, buildSchema(
        QStringLiteral("trueDarts"), QStringLiteral("operation"),
        QStringLiteral("Creates the two \"true\" (folded) dart-leg endpoints geometrically, from the dart's base line and its three defining points. No formula involved."),
        {
            param(QStringLiteral("point1Name"), QStringLiteral("string"), true, QStringLiteral("Name for the first new result point (not an existing name).")),
            param(QStringLiteral("point2Name"), QStringLiteral("string"), true, QStringLiteral("Name for the second new result point (not an existing name).")),
            param(QStringLiteral("baseLineP1"), QStringLiteral("string"), true, QStringLiteral("First point of the dart's base line.")),
            param(QStringLiteral("baseLineP2"), QStringLiteral("string"), true, QStringLiteral("Second point of the dart's base line.")),
            param(QStringLiteral("dartP1"), QStringLiteral("string"), true, QStringLiteral("First point defining the dart.")),
            param(QStringLiteral("dartP2"), QStringLiteral("string"), true, QStringLiteral("Second point defining the dart.")),
            param(QStringLiteral("dartP3"), QStringLiteral("string"), true, QStringLiteral("Third point defining the dart.")),
            param(QStringLiteral("mx1"), QStringLiteral("number"), false, QStringLiteral("Literal label x-offset for the first result point."), QStringLiteral("0")),
            param(QStringLiteral("my1"), QStringLiteral("number"), false, QStringLiteral("Literal label y-offset for the first result point."), QStringLiteral("0")),
            param(QStringLiteral("showPointName1"), QStringLiteral("boolean"), false, QStringLiteral("Literal flag: show the first result point's name label."), QStringLiteral("true")),
            param(QStringLiteral("mx2"), QStringLiteral("number"), false, QStringLiteral("Literal label x-offset for the second result point."), QStringLiteral("0")),
            param(QStringLiteral("my2"), QStringLiteral("number"), false, QStringLiteral("Literal label y-offset for the second result point."), QStringLiteral("0")),
            param(QStringLiteral("showPointName2"), QStringLiteral("boolean"), false, QStringLiteral("Literal flag: show the second result point's name label."), QStringLiteral("true")),
        },
        QStringLiteral(R"({ "op": "trueDarts", "point1Name": "T1", "point2Name": "T2", "baseLineP1": "A", "baseLineP2": "B", "dartP1": "D1", "dartP2": "D2", "dartP3": "D3" })")));

    // ---- Pieces --------------------------------------------------------------------------------
    registerAction(QStringLiteral("piece.addPatternPiece"), &handlePieceAddPatternPiece, buildSchema(
        QStringLiteral("piece.addPatternPiece"), QStringLiteral("piece"),
        QStringLiteral("Closes a list of point names into one named pattern piece, with a formula seam-allowance width."),
        {
            param(QStringLiteral("name"), QStringLiteral("string"), true, QStringLiteral("Piece name.")),
            param(QStringLiteral("nodes"), QStringLiteral("array"), true,
                QStringLiteral("At least 3 point names (curve/arc nodes are not supported yet), which must form a closed, non-self-intersecting polygon."),
                QString(), QStringLiteral("string")),
            param(QStringLiteral("seamAllowanceWidth"), QStringLiteral("string"), true,
                QStringLiteral("Formula string for the seam allowance width, evaluated by Seamly2D's formula engine.")),
            param(QStringLiteral("seamAllowance"), QStringLiteral("boolean"), false,
                QStringLiteral("Literal flag: whether seam allowance is actually enabled (independent of supplying a width formula)."), QStringLiteral("true")),
            param(QStringLiteral("fill"), QStringLiteral("string"), false, QStringLiteral("Piece fill pattern name."), QStringLiteral("FillNone")),
            param(QStringLiteral("pieceColor"), QStringLiteral("string"), false, QStringLiteral("Piece outline color. Only set if given at all; no default applied.")),
        },
        QStringLiteral(R"({ "op": "piece.addPatternPiece", "name": "Square", "nodes": ["A", "B", "C", "D"], "seamAllowanceWidth": "10" })")));

    registerAction(QStringLiteral("piece.addAnchorPoint"), &handlePieceAddAnchorPoint, buildSchema(
        QStringLiteral("piece.addAnchorPoint"), QStringLiteral("piece"),
        QStringLiteral("Attaches an existing point to an existing piece as a label anchor."),
        {
            param(QStringLiteral("point"), QStringLiteral("string"), true, QStringLiteral("Name of the existing point to use as the anchor.")),
            param(QStringLiteral("piece"), QStringLiteral("string"), true, QStringLiteral("Name of the existing piece (created by piece.addPatternPiece).")),
        },
        QStringLiteral(R"({ "op": "piece.addAnchorPoint", "point": "A", "piece": "Square" })")));

    registerAction(QStringLiteral("piece.internalPath"), &handlePieceInternalPath, buildSchema(
        QStringLiteral("piece.internalPath"), QStringLiteral("piece"),
        QStringLiteral("Adds an internal path (e.g. a dart or pocket guideline) through a list of point names, within an existing piece. Unlike piece.addPatternPiece, the path does not need to be closed."),
        {
            param(QStringLiteral("piece"), QStringLiteral("string"), true, QStringLiteral("Name of the existing piece.")),
            param(QStringLiteral("nodes"), QStringLiteral("array"), true,
                QStringLiteral("At least 2 point names (curve/arc nodes are not supported yet)."), QString(), QStringLiteral("string")),
            param(QStringLiteral("lineColor"), QStringLiteral("string"), false, QStringLiteral("Visual path color.")),
            param(QStringLiteral("lineWeight"), QStringLiteral("string"), false, QStringLiteral("Visual path weight.")),
            param(QStringLiteral("cutPath"), QStringLiteral("boolean"), false, QStringLiteral("Literal flag: whether this path should be cut through, not just drawn."), QStringLiteral("false")),
        },
        QStringLiteral(R"({ "op": "piece.internalPath", "piece": "Square", "nodes": ["A", "C"] })")));

    registerAction(QStringLiteral("piece.insertNodes"), &handlePieceInsertNodes, buildSchema(
        QStringLiteral("piece.insertNodes"), QStringLiteral("piece"),
        QStringLiteral("Appends additional point nodes onto an existing piece's outline path."),
        {
            param(QStringLiteral("piece"), QStringLiteral("string"), true, QStringLiteral("Name of the existing piece.")),
            param(QStringLiteral("nodes"), QStringLiteral("array"), true,
                QStringLiteral("Non-empty list of point names to append."), QString(), QStringLiteral("string")),
        },
        QStringLiteral(R"({ "op": "piece.insertNodes", "piece": "Square", "nodes": ["E"] })"),
        true,
        QStringLiteral("One reproduced (caught, non-fatal) failure path inside SavePieceOptions' XML-serialization was not root-caused -- see docs/action-layer-schema.md's own KNOWN GAP note on this op. The op still succeeds in the common case; this is a documented reliability caveat, not a block on using it.")));

    registerAction(QStringLiteral("piece.union"), &handlePieceUnion, buildSchema(
        QStringLiteral("piece.union"), QStringLiteral("piece"),
        QStringLiteral("Unites two existing pieces along a chosen pair of edges into one piece."),
        {
            param(QStringLiteral("piece1"), QStringLiteral("string"), true, QStringLiteral("Name of the first piece.")),
            param(QStringLiteral("piece2"), QStringLiteral("string"), true, QStringLiteral("Name of the second piece.")),
            param(QStringLiteral("piece1EdgeIndex"), QStringLiteral("number"), true,
                QStringLiteral("Literal 0-based index of the edge on piece1 to unite along.")),
            param(QStringLiteral("piece2EdgeIndex"), QStringLiteral("number"), true,
                QStringLiteral("Literal 0-based index of the edge on piece2 to unite along.")),
            param(QStringLiteral("retainPieces"), QStringLiteral("boolean"), false,
                QStringLiteral("Literal flag: keep the two source pieces around after uniting."), QStringLiteral("false")),
        },
        QStringLiteral(R"({ "op": "piece.union", "piece1": "Front", "piece2": "Back", "piece1EdgeIndex": 0, "piece2EdgeIndex": 0 })"),
        true,
        QStringLiteral("DO NOT USE YET: reproducibly segfaults inside UnionTool::Create() against real piece data, regardless of edge-index correctness -- see docs/action-layer-schema.md's own KNOWN GAP note on this op. Still registered/documented so its parameters are visible ahead of a fix.")));

    // ---- Measurements ----------------------------------------------------------------------------
    registerAction(QStringLiteral("measurements.load"), &handleMeasurementsLoad, buildSchema(
        QStringLiteral("measurements.load"), QStringLiteral("measurements"),
        QStringLiteral("Loads a measurement file (.smis/.smms/.vst) into the pattern, replacing whatever measurements were previously loaded. Does NOT recompute pattern geometry -- call measurements.recompute (or use measurements.sync) for that."),
        {
            param(QStringLiteral("path"), QStringLiteral("string"), true, QStringLiteral("Measurement file path.")),
            param(QStringLiteral("size"), QStringLiteral("number"), false,
                QStringLiteral("Literal size value. Required only for a multisize measurement file; applied verbatim with no unit conversion. Ignored for an individual-measurement file.")),
            param(QStringLiteral("height"), QStringLiteral("number"), false,
                QStringLiteral("Literal height value. Required only for a multisize measurement file; applied verbatim with no unit conversion. Ignored for an individual-measurement file.")),
        },
        QStringLiteral(R"({ "op": "measurements.load", "path": "sizes.smis" })")));

    registerAction(QStringLiteral("measurements.recompute"), &handleMeasurementsRecompute, buildSchema(
        QStringLiteral("measurements.recompute"), QStringLiteral("measurements"),
        QStringLiteral("Re-evaluates every formula in the pattern against the currently loaded measurement/variable values and rebuilds geometry. Useful standalone, not only after a measurement change."),
        {},
        QStringLiteral(R"({ "op": "measurements.recompute" })")));

    registerAction(QStringLiteral("measurements.sync"), &handleMeasurementsSync, buildSchema(
        QStringLiteral("measurements.sync"), QStringLiteral("measurements"),
        QStringLiteral("Equivalent to measurements.load immediately followed by measurements.recompute, in one action."),
        {
            param(QStringLiteral("path"), QStringLiteral("string"), true, QStringLiteral("Measurement file path.")),
            param(QStringLiteral("size"), QStringLiteral("number"), false,
                QStringLiteral("Literal size value. Required only for a multisize measurement file. Ignored for an individual-measurement file.")),
            param(QStringLiteral("height"), QStringLiteral("number"), false,
                QStringLiteral("Literal height value. Required only for a multisize measurement file. Ignored for an individual-measurement file.")),
        },
        QStringLiteral(R"({ "op": "measurements.sync", "path": "sizes.smis" })")));

    // ---- Session lifecycle -----------------------------------------------------------------------
    registerAction(QStringLiteral("session.save"), &handleSessionSave, buildSchema(
        QStringLiteral("session.save"), QStringLiteral("session"),
        QStringLiteral("Saves the current (possibly mutated) pattern to a .val file. In-script equivalent of the one-shot CLI's --save-pattern flag."),
        {
            param(QStringLiteral("path"), QStringLiteral("string"), true,
                QStringLiteral("Output .val path. Resolved relative to the process's current working directory. Parent directory created if missing.")),
        },
        QStringLiteral(R"({ "op": "session.save", "path": "square.val" })")));

    registerAction(QStringLiteral("session.close"), &handleSessionClose, buildSchema(
        QStringLiteral("session.close"), QStringLiteral("session"),
        QStringLiteral("Ends the persistent NDJSON daemon's read loop after this batch's response is written. No effect in one-shot mode beyond succeeding as a normal action."),
        {},
        QStringLiteral(R"({ "op": "session.close" })")));
}

// Stores the handler function and its descriptive schema in the internal maps under the given
// name, overwriting any prior entry with that name in both maps together.
void ActionRegistry::registerAction(const QString &name, ActionFn fn, ActionSchema schemaValue)
{
    m_actions.insert(name, fn);          // QHash::insert adds the entry, replacing any existing one with the same key.
    m_schemas.insert(name, schemaValue); // Kept in lockstep with m_actions: every name in one map has a matching entry in the other.
}

// Reports whether a handler has been registered for the given name.
bool ActionRegistry::hasAction(const QString &name) const
{
    return m_actions.contains(name); // QHash::contains performs the presence check.
}

// Looks up the handler registered under the given name, if any.
ActionRegistry::ActionFn ActionRegistry::action(const QString &name) const
{
    return m_actions.value(name); // QHash::value returns a default-constructed (empty/falsy) ActionFn when the key is absent.
}

// Returns how many handlers are actually registered, read from m_actions directly (not m_schemas).
int ActionRegistry::actionCount() const
{
    return m_actions.size();
}

// Looks up the schema registered under the given name, if any.
const ActionSchema *ActionRegistry::schema(const QString &name) const
{
    const auto it = m_schemas.constFind(name); // QHash::constFind avoids QHash::value()'s copy-on-miss default-construction cost.
    if (it == m_schemas.constEnd())
    {
        return nullptr; // No schema registered under this name (implies no handler either, since both are always inserted together).
    }
    return &it.value(); // Valid as long as this ActionRegistry (and therefore m_schemas) is alive.
}

// Returns every registered schema, sorted alphabetically by op name for a stable, deterministic
// listing order.
QVector<ActionSchema> ActionRegistry::allSchemas() const
{
    QVector<ActionSchema> result;
    result.reserve(m_schemas.size());
    for (auto it = m_schemas.constBegin(); it != m_schemas.constEnd(); ++it)
    {
        result.append(it.value());
    }
    std::sort(result.begin(), result.end(), [](const ActionSchema &a, const ActionSchema &b) {
        return a.op < b.op;
    });
    return result;
}
