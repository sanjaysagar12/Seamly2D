//---------------------------------------------------------------------------------------------------------------------
//  @file   pattern_dump_handler.cpp
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

#include "pattern_dump_handler.h" // Brings in the ActionResult-returning handlePatternDump declaration this file implements.

#include "../action_context.h" // Brings in ActionContext, supplying scene/doc/data for this handler.

#include "../../vpatterndb/vcontainer.h"  // Brings in VContainer::DataGObjects(), the source of every geometry object.
#include "../../vgeometry/vgobject.h"     // Brings in VGObject: name(), getType().
#include "../../vgeometry/vpointf.h"      // Brings in VPointF: x(), y(), used only for GOType::Point objects.
#include "../../vgeometry/vgeometrydef.h" // Brings in the GOType enum classified by goTypeToString() below.
#include "../../ifc/xml/vabstractpattern.h" // Brings in VAbstractPattern::getHistory() (tool-history entries) and the static getTool(id) used below to reach a point's live tool instance.
#include "../../ifc/xml/vtoolrecord.h"      // Brings in VToolRecord: getId(), getTypeTool(), getDraftBlockName().
#include "../../ifc/exception/vexception.h" // Brings in VExceptionBadId, thrown by getTool() for a point with no registered tool (Phase 8: move/rotation/mirror destination points).
#include "../../vmisc/def.h"                // Brings in the Tool enum classified by toolToString() below.
#include "../../vpatterndb/vformula.h"      // Brings in VFormula/FormulaType, used to read a formula-bearing tool's raw (non-localized) formula string.

// Phase 6: pulls in the two tool classes whose formula strings pattern.dump now reports.
// actionlayer.pro does not link libvtools itself -- only actiond.pro/ActionLayerTest.pro do, for
// the final executable -- so this is a header-only dependency, the same idiom render_handlers.cpp
// already relies on for vdatatool.h (see that file's own comment on the point).
#include "../../vtools/tools/vdatatool.h" // Brings in VDataTool, the type VAbstractPattern::getTool() returns.
#include "../../vtools/tools/drawTools/toolpoint/toolsinglepoint/toollinepoint/vtoollinepoint.h" // Brings in VToolLinePoint::GetFormulaLength(), shared by endLine/alongLine/normal/bisector/shoulderPoint.
#include "../../vtools/tools/drawTools/toolpoint/toolsinglepoint/toollinepoint/vtoolendline.h"   // Brings in VToolEndLine::GetFormulaAngle(), the one extra formula only endLine has.

#include <QDebug>       // Provides qWarning(), used to log any enumerator this file doesn't yet know how to name.
#include <QJsonArray>   // Provides QJsonArray, used to build the "objects" and "history" JSON arrays.
#include <QJsonObject>  // Provides QJsonObject, used for each per-object/per-history-entry JSON record.
#include <QSharedPointer> // Provides QSharedPointer and qSharedPointerDynamicCast, used to reach VPointF-specific data.

// Converts a GOType enumerator to its exact C++ name, so JSON consumers get a stable,
// machine-readable label instead of a numeric value. Every enumerator is listed explicitly;
// an unmapped value (e.g. from a future enumerator) falls through to the logged "Unknown" case.
// Declared in pattern_dump_handler.h (not file-local) so pattern_resolve_name_handler.cpp can
// reuse this exact stringification instead of duplicating it.
QString goTypeToString(GOType type)
{
    switch (type)
    {
        case GOType::Point:           return QStringLiteral("Point");           // Single point object.
        case GOType::Arc:              return QStringLiteral("Arc");              // Circular arc object.
        case GOType::EllipticalArc:    return QStringLiteral("EllipticalArc");    // Elliptical arc object.
        case GOType::Spline:           return QStringLiteral("Spline");           // Quadratic spline object.
        case GOType::SplinePath:       return QStringLiteral("SplinePath");       // Multi-point spline path object.
        case GOType::CubicBezier:      return QStringLiteral("CubicBezier");      // Cubic bezier curve object.
        case GOType::CubicBezierPath:  return QStringLiteral("CubicBezierPath");  // Multi-point cubic bezier path object.
        case GOType::Unknown:          return QStringLiteral("Unknown");          // Explicit "not yet classified" enumerator.
        case GOType::Curve:            return QStringLiteral("Curve");            // Generic curve category marker.
        case GOType::Path:             return QStringLiteral("Path");             // Generic path category marker.
        case GOType::AllCurves:        return QStringLiteral("AllCurves");        // "Any curve type" category marker.
    }

    qWarning() << "goTypeToString: unmapped GOType value" << static_cast<int>(type); // Log so a future enumerator gets noticed, not silently misreported.
    return QStringLiteral("Unknown"); // Never crash on an unrecognized value; fail loudly via the warning above instead.
}

namespace
{
    // Converts a Tool enumerator to its exact C++ name, mirroring goTypeToString()'s contract:
    // every current enumerator is listed explicitly, and anything unmapped logs a warning and
    // reports "Unknown" instead of crashing.
    QString toolToString(Tool tool)
    {
        switch (tool)
        {
            case Tool::Arrow:                        return QStringLiteral("Arrow");                        // Selection/arrow tool.
            case Tool::SinglePoint:                   return QStringLiteral("SinglePoint");                   // Generic single-point visualization.
            case Tool::DoublePoint:                   return QStringLiteral("DoublePoint");                   // Generic two-point visualization.
            case Tool::LinePoint:                     return QStringLiteral("LinePoint");                     // Generic point-on-line visualization.
            case Tool::AbstractSpline:                return QStringLiteral("AbstractSpline");                // Generic spline visualization base.
            case Tool::Cut:                           return QStringLiteral("Cut");                           // Generic cut-curve base tool.
            case Tool::BasePoint:                     return QStringLiteral("BasePoint");                     // Draft block origin point tool.
            case Tool::EndLine:                       return QStringLiteral("EndLine");                       // Point-at-distance-and-angle tool.
            case Tool::Line:                          return QStringLiteral("Line");                          // Straight line tool.
            case Tool::AlongLine:                     return QStringLiteral("AlongLine");                     // Point-along-line tool.
            case Tool::ShoulderPoint:                 return QStringLiteral("ShoulderPoint");                 // Shoulder point tool.
            case Tool::Normal:                        return QStringLiteral("Normal");                        // Point-via-normal tool.
            case Tool::Bisector:                      return QStringLiteral("Bisector");                      // Angle bisector point tool.
            case Tool::LineIntersect:                 return QStringLiteral("LineIntersect");                 // Line/line intersection point tool.
            case Tool::Spline:                        return QStringLiteral("Spline");                        // Quadratic spline tool.
            case Tool::CubicBezier:                   return QStringLiteral("CubicBezier");                   // Cubic bezier curve tool.
            case Tool::CutSpline:                     return QStringLiteral("CutSpline");                     // Cut-spline tool.
            case Tool::CutArc:                        return QStringLiteral("CutArc");                        // Cut-arc tool.
            case Tool::Arc:                           return QStringLiteral("Arc");                           // Arc-by-radius tool.
            case Tool::ArcWithLength:                 return QStringLiteral("ArcWithLength");                 // Arc-by-length tool.
            case Tool::SplinePath:                    return QStringLiteral("SplinePath");                    // Multi-point spline path tool.
            case Tool::CubicBezierPath:                return QStringLiteral("CubicBezierPath");               // Multi-point cubic bezier path tool.
            case Tool::CutSplinePath:                 return QStringLiteral("CutSplinePath");                 // Cut-spline-path tool.
            case Tool::PointOfContact:                return QStringLiteral("PointOfContact");                // Point-of-contact tool.
            case Tool::Piece:                         return QStringLiteral("Piece");                         // Pattern piece tool.
            case Tool::InternalPath:                  return QStringLiteral("InternalPath");                  // Internal path tool.
            case Tool::NodePoint:                     return QStringLiteral("NodePoint");                     // Piece-node point tool.
            case Tool::NodeArc:                       return QStringLiteral("NodeArc");                       // Piece-node arc tool.
            case Tool::NodeElArc:                     return QStringLiteral("NodeElArc");                     // Piece-node elliptical arc tool.
            case Tool::NodeSpline:                    return QStringLiteral("NodeSpline");                    // Piece-node spline tool.
            case Tool::NodeSplinePath:                return QStringLiteral("NodeSplinePath");                // Piece-node spline path tool.
            case Tool::Height:                        return QStringLiteral("Height");                        // Height (perpendicular) point tool.
            case Tool::Triangle:                      return QStringLiteral("Triangle");                      // Triangle point tool.
            case Tool::LineIntersectAxis:              return QStringLiteral("LineIntersectAxis");             // Line/axis intersection point tool.
            case Tool::PointOfIntersectionArcs:       return QStringLiteral("PointOfIntersectionArcs");       // Arc/arc intersection point tool.
            case Tool::PointOfIntersectionCircles:    return QStringLiteral("PointOfIntersectionCircles");    // Circle/circle intersection point tool.
            case Tool::PointOfIntersectionCurves:     return QStringLiteral("PointOfIntersectionCurves");     // Curve/curve intersection point tool.
            case Tool::CurveIntersectAxis:             return QStringLiteral("CurveIntersectAxis");            // Curve/axis intersection point tool.
            case Tool::ArcIntersectAxis:               return QStringLiteral("ArcIntersectAxis");              // Arc/axis intersection point tool (reserved, unused by any live tool).
            case Tool::PointOfIntersection:           return QStringLiteral("PointOfIntersection");           // Generic line/line intersection point tool.
            case Tool::PointFromCircleAndTangent:     return QStringLiteral("PointFromCircleAndTangent");     // Circle-and-tangent point tool.
            case Tool::PointFromArcAndTangent:        return QStringLiteral("PointFromArcAndTangent");        // Arc-and-tangent point tool.
            case Tool::TrueDarts:                     return QStringLiteral("TrueDarts");                     // True-darts tool.
            case Tool::Union:                         return QStringLiteral("Union");                         // Piece union tool.
            case Tool::Group:                         return QStringLiteral("Group");                         // Object group tool.
            case Tool::Rotation:                       return QStringLiteral("Rotation");                      // Rotation operation tool.
            case Tool::MirrorByLine:                  return QStringLiteral("MirrorByLine");                  // Mirror-by-line operation tool.
            case Tool::MirrorByAxis:                  return QStringLiteral("MirrorByAxis");                  // Mirror-by-axis operation tool.
            case Tool::Move:                          return QStringLiteral("Move");                          // Move operation tool.
            case Tool::Midpoint:                      return QStringLiteral("Midpoint");                      // Midpoint tool (shares AlongLine's tool type in practice).
            case Tool::EllipticalArc:                 return QStringLiteral("EllipticalArc");                 // Elliptical arc tool.
            case Tool::AnchorPoint:                    return QStringLiteral("AnchorPoint");                   // Label anchor point tool.
            case Tool::InsertNodes:                   return QStringLiteral("InsertNodes");                   // Insert-nodes tool.
            case Tool::BackgroundImage:                return QStringLiteral("BackgroundImage");               // Background image tool.
            case Tool::LAST_ONE_DO_NOT_USE:           return QStringLiteral("LAST_ONE_DO_NOT_USE");           // Sentinel value; never a real tool, mapped only so the switch stays exhaustive.
        }

        qWarning() << "toolToString: unmapped Tool value" << static_cast<int>(tool); // Log so a future enumerator gets noticed, not silently misreported.
        return QStringLiteral("Unknown"); // Never crash on an unrecognized value; fail loudly via the warning above instead.
    }
}

// Implements "pattern.dump": a read-only snapshot of every geometry object and every tool
// history entry currently in the pattern, returned as {"objects": [...], "history": [...]}.
ActionResult handlePatternDump(const QJsonObject &args, const ActionContext &ctx)
{
    Q_UNUSED(args) // pattern.dump takes no arguments in Phase 1; kept in the signature for uniformity with other handlers.

    QJsonArray objects; // Accumulates one JSON object per geometry object found in the container.

    const VContainer *data = ctx.data(); // Local alias for the pattern's variable/data container.
    if (data != nullptr) // Guard against a context that was constructed without a data container.
    {
        const QHash<quint32, QSharedPointer<VGObject>> *gObjects = data->DataGObjects(); // Every geometry object, keyed by id.
        if (gObjects != nullptr) // DataGObjects() can return nullptr before a pattern has been parsed.
        {
            for (auto it = gObjects->constBegin(); it != gObjects->constEnd(); ++it) // Walk every entry in the hash.
            {
                const QSharedPointer<VGObject> &obj = it.value(); // The current geometry object, shared-pointer owned.
                if (obj.isNull()) // Defensive guard: skip a null entry instead of dereferencing it.
                {
                    continue; // Nothing to report for a null object; move on to the next entry.
                }

                QJsonObject entry; // Builds this one object's JSON record.
                entry["id"] = static_cast<qint64>(it.key()); // The object's own id is the DataGObjects() hash key, not getIdObject() (that returns a *parent* id, e.g. an arc's center point, and is 0 for a standalone object).
                entry["name"] = obj->name(); // The object's user-assigned or generated name.
                entry["type"] = goTypeToString(obj->getType()); // Machine-readable geometry-type label.

                if (obj->getType() == GOType::Point) // Only point objects carry x/y coordinates worth exposing here.
                {
                    const QSharedPointer<VPointF> point = qSharedPointerDynamicCast<VPointF>(obj); // Downcast; safe since getType() already confirmed this is a point.
                    if (!point.isNull()) // Guard against an unexpected cast failure.
                    {
                        entry["x"] = point->x(); // Point's x coordinate, in the pattern's working units.
                        entry["y"] = point->y(); // Point's y coordinate, in the pattern's working units.
                    }

                    // Phase 6: expose the raw formula string(s) behind a formula-bearing point, so
                    // fixtures/consumers can diff/inspect pattern structure (not just resolved
                    // geometry) without re-deriving it from the XML. getTool() is a process-wide
                    // static lookup (VAbstractPattern::tools), keyed by the same id every one of
                    // this file's Create()-based tools registers itself under via
                    // VAbstractPattern::AddTool(id, this) -- the identical idiom render_handlers.cpp
                    // already uses to resolve a highlight name's live tool instance.
                    //
                    // Phase 8: unlike every Phase 1-7 handler, VToolMove/VToolRotation/
                    // VToolMirrorByLine/VToolMirrorByAxis (operation_handlers.cpp) create
                    // destination points that are plain VPointF objects added straight via
                    // VContainer::AddGObject(), with no individual VDataTool registered at their
                    // own id (only the *operation* tool itself, at a different id, is registered) --
                    // a state pattern.dump now has to expect. getTool() throws VExceptionBadId
                    // (not nullptr) for an id with no registered tool, so that specific, expected
                    // "no tool for this point" case is caught here and simply skips the
                    // formulaLength/formulaAngle fields for this point, rather than the exception
                    // propagating uncaught out of ActionEngine::run() (which only catches
                    // ActionResolverError) and crashing the whole batch.
                    try
                    {
                        VDataTool *pointTool = VAbstractPattern::getTool(it.key());
                        // VToolLinePoint is the common base of endLine/alongLine/normal/bisector/
                        // shoulderPoint (VToolLineIntersect is not one -- it has no formula at all, so
                        // this cast simply fails for it, exactly as intended). FormulaType::FromUser
                        // reverses the ToUser-side translateVariables() round trip VFormula's
                        // constructor performs, giving back the same non-localized formula string this
                        // action layer's own endLine/alongLine/normal/bisector/shoulderPoint handlers
                        // passed into Create() (see formula_point_handlers.cpp), not a GUI-localized one.
                        if (VToolLinePoint *linePointTool = qobject_cast<VToolLinePoint *>(pointTool))
                        {
                            entry["formulaLength"] = linePointTool->GetFormulaLength().GetFormula(FormulaType::FromUser);

                            // VToolEndLine is the one tool in this family with a second (angle) formula;
                            // every other VToolLinePoint subclass either has no angle at all (alongLine,
                            // bisector, shoulderPoint) or a plain numeric one, not a formula (normal --
                            // see formula_point_handlers.cpp's handleNormal() comment).
                            if (VToolEndLine *endLineTool = qobject_cast<VToolEndLine *>(pointTool))
                            {
                                entry["formulaAngle"] = endLineTool->GetFormulaAngle().GetFormula(FormulaType::FromUser);
                            }
                        }
                    }
                    catch (const VExceptionBadId &)
                    {
                        // No tool registered for this point id -- see the comment above; not an error.
                    }
                }

                objects.append(entry); // Add this object's JSON record to the output array.
            }
        }
    }

    QJsonArray history; // Accumulates one JSON object per tool-history entry.

    VAbstractPattern *doc = ctx.doc(); // Local alias for the pattern document.
    if (doc != nullptr) // Guard against a context that was constructed without a document.
    {
        QVector<VToolRecord> *records = doc->getHistory(); // Ordered list of every tool applied to the pattern.
        if (records != nullptr) // getHistory() can return nullptr before a document has been parsed.
        {
            for (const VToolRecord &record : *records) // Walk the history in application order.
            {
                QJsonObject entry; // Builds this one history entry's JSON record.
                entry["id"] = static_cast<qint64>(record.getId()); // Widen quint32 losslessly into qint64 for JSON.
                entry["tool"] = toolToString(record.getTypeTool()); // Machine-readable tool-type label.
                entry["draftBlock"] = record.getDraftBlockName(); // Name of the draft block (pattern piece) this tool belongs to.
                history.append(entry); // Add this history entry's JSON record to the output array.
            }
        }
    }

    QJsonObject payload; // Combines both arrays under their documented output keys.
    payload["objects"] = objects; // "objects": every geometry object currently in the container.
    payload["history"] = history; // "history": every tool-history entry currently in the document.

    return ActionResult::success(payload); // Wrap the payload as a successful result for the registry/engine to return.
}
