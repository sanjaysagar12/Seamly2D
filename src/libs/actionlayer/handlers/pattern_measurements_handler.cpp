//---------------------------------------------------------------------------------------------------------------------
//  @file   pattern_measurements_handler.cpp
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

#include "pattern_measurements_handler.h" // Brings in the ActionResult-returning handleListMeasurements declaration this file implements.

#include "../action_context.h" // Brings in ActionContext, supplying the data container for this handler.

#include "../../vpatterndb/vcontainer.h"                       // Brings in VContainer::DataMeasurements().
#include "../../vpatterndb/variables/measurement_variable.h"   // Brings in MeasurementVariable: GetFormula(), GetValue().

#include <QJsonArray>  // Provides QJsonArray, used to build the "measurements" JSON array.
#include <QJsonObject> // Provides QJsonObject, used for each per-measurement JSON record.
#include <QSharedPointer> // Provides QSharedPointer, the storage type DataMeasurements() values use.

// Implements "pattern.listMeasurements": a read-only snapshot of every measurement variable
// currently attached to the pattern, returned as {"measurements": [...]}. An empty array is a
// valid, non-error result for a pattern with no measurements attached.
ActionResult handleListMeasurements(const QJsonObject &args, const ActionContext &ctx)
{
    Q_UNUSED(args) // pattern.listMeasurements takes no arguments in Phase 1; kept for signature uniformity with other handlers.

    QJsonArray measurements; // Accumulates one JSON object per measurement variable.

    const VContainer *data = ctx.data(); // Local alias for the pattern's variable/data container.
    if (data != nullptr) // Guard against a context that was constructed without a data container.
    {
        const QMap<QString, QSharedPointer<MeasurementVariable>> table = data->DataMeasurements(); // Snapshot of every measurement, keyed by name.
        for (auto it = table.constBegin(); it != table.constEnd(); ++it) // Walk every entry in the map, in key order.
        {
            const QSharedPointer<MeasurementVariable> &shared = it.value(); // Shared pointer to the current measurement.
            if (shared.isNull()) // Defensive guard: skip a null entry instead of dereferencing it.
            {
                continue; // Nothing to report for a null measurement; move on to the next entry.
            }

            const MeasurementVariable *variable = shared.data(); // Raw const pointer forces the const GetValue() overload below, not the qreal* one.

            QJsonObject entry; // Builds this one measurement's JSON record.
            entry["name"] = it.key(); // The measurement's name, taken from the QMap key per the spec.
            entry["formula"] = variable->GetFormula(); // The formula string backing this measurement's value.
            entry["value"] = variable->GetValue(); // The measurement's current numeric value.

            measurements.append(entry); // Add this measurement's JSON record to the output array.
        }
    }

    QJsonObject payload; // Wraps the array under its documented output key.
    payload["measurements"] = measurements; // "measurements": every measurement variable currently attached to the pattern.

    return ActionResult::success(payload); // Wrap the payload as a successful result; an empty array here is not an error.
}
