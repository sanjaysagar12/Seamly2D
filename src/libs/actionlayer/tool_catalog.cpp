//---------------------------------------------------------------------------------------------------------------------
//  @file   tool_catalog.cpp
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

#include "tool_catalog.h" // Brings in the ToolCatalog::renderHuman()/renderAi() declarations this file implements.

#include "action_registry.h" // Brings in ActionRegistry::allSchemas(), the data source for both renderers below.
#include "action_schema.h"   // Brings in ActionSchema/ActionParamSchema, iterated over below.

#include <QJsonArray>
#include <QJsonObject>
#include <QPair>
#include <QStringList>
#include <QVector>

namespace
{
    // Category display order, matching docs/action-layer-schema.md's own section headings exactly
    // (## Read-only introspection, ## Points, ## Formula-bearing points, ## Curves, ## Cut /
    // intersection points, ## Operations, ## Pieces, ## Measurements, ## Session lifecycle) -- see
    // action_registry.cpp's registerBuiltinActions() for where each op's "category" string is set.
    const QVector<QPair<QString, QString>> &categoryOrder()
    {
        static const QVector<QPair<QString, QString>> order = {
            { QStringLiteral("introspection"), QStringLiteral("Read-only introspection") },
            { QStringLiteral("point"), QStringLiteral("Points") },
            { QStringLiteral("formula-point"), QStringLiteral("Formula-bearing points") },
            { QStringLiteral("curve"), QStringLiteral("Curves") },
            { QStringLiteral("cut-point"), QStringLiteral("Cut / intersection points") },
            { QStringLiteral("operation"), QStringLiteral("Operations") },
            { QStringLiteral("piece"), QStringLiteral("Pieces") },
            { QStringLiteral("measurements"), QStringLiteral("Measurements") },
            { QStringLiteral("session"), QStringLiteral("Session lifecycle") },
        };
        return order;
    }

    // "required" / "optional (default: X)" / "optional", used as one column in the human-readable
    // parameter table.
    QString requiredColumn(const ActionParamSchema &p)
    {
        if (p.required)
        {
            return QStringLiteral("required");
        }
        if (!p.defaultValue.isEmpty())
        {
            return QStringLiteral("optional (default: %1)").arg(p.defaultValue);
        }
        return QStringLiteral("optional");
    }

    void appendParamTable(QString &out, const QVector<ActionParamSchema> &parameters)
    {
        if (parameters.isEmpty())
        {
            out += QStringLiteral("    Parameters: (none)\n");
            return;
        }

        // Column widths sized to the actual data, so the table stays readable without hardcoding a
        // width that might be too narrow for a future longer field/type name.
        int nameWidth = 0;
        int typeWidth = 0;
        int reqWidth = 0;
        QVector<QString> reqColumns;
        reqColumns.reserve(parameters.size());
        for (const ActionParamSchema &p : parameters)
        {
            nameWidth = qMax(nameWidth, p.name.length());
            QString type = p.jsonType;
            if (p.jsonType == QStringLiteral("array") && !p.itemsType.isEmpty())
            {
                type = QStringLiteral("array<%1>").arg(p.itemsType);
            }
            typeWidth = qMax(typeWidth, type.length());
            const QString reqCol = requiredColumn(p);
            reqColumns.append(reqCol);
            reqWidth = qMax(reqWidth, reqCol.length());
        }

        out += QStringLiteral("    Parameters:\n");
        for (int i = 0; i < parameters.size(); ++i)
        {
            const ActionParamSchema &p = parameters.at(i);
            QString type = p.jsonType;
            if (p.jsonType == QStringLiteral("array") && !p.itemsType.isEmpty())
            {
                type = QStringLiteral("array<%1>").arg(p.itemsType);
            }
            out += QStringLiteral("      %1  %2  %3  %4\n")
                       .arg(p.name.leftJustified(nameWidth))
                       .arg(type.leftJustified(typeWidth))
                       .arg(reqColumns.at(i).leftJustified(reqWidth))
                       .arg(p.description);
        }
    }

    QJsonObject paramToJsonSchemaProperty(const ActionParamSchema &p)
    {
        QJsonObject prop;
        prop[QStringLiteral("type")] = p.jsonType;
        prop[QStringLiteral("description")] = p.description;

        if (p.jsonType == QStringLiteral("array") && !p.itemsType.isEmpty())
        {
            QJsonObject items;
            items[QStringLiteral("type")] = p.itemsType;
            prop[QStringLiteral("items")] = items;
        }

        if (!p.defaultValue.isEmpty())
        {
            // JSON Schema's own convention is a typed default, not a string -- match the field's
            // declared jsonType so e.g. a boolean's default is JSON `true`, not the string "true".
            if (p.jsonType == QStringLiteral("boolean"))
            {
                prop[QStringLiteral("default")] = (p.defaultValue.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0);
            }
            else if (p.jsonType == QStringLiteral("number"))
            {
                bool ok = false;
                const double value = p.defaultValue.toDouble(&ok);
                if (ok)
                {
                    prop[QStringLiteral("default")] = value;
                }
                else
                {
                    prop[QStringLiteral("default")] = p.defaultValue; // Fallback: an unparsable default is still reported, as a string, rather than silently dropped.
                }
            }
            else
            {
                prop[QStringLiteral("default")] = p.defaultValue;
            }
        }

        return prop;
    }
}

QString ToolCatalog::renderHuman(const ActionRegistry &registry)
{
    const QVector<ActionSchema> schemas = registry.allSchemas(); // Already sorted alphabetically by op name.

    QString out;
    out += QStringLiteral("actiond action catalog (%1 ops)\n").arg(schemas.size());
    out += QStringLiteral("Generated live from ActionRegistry -- see docs/action-layer-schema.md for prose narrative.\n");

    for (const auto &categoryEntry : categoryOrder())
    {
        const QString &categoryKey = categoryEntry.first;
        const QString &categoryTitle = categoryEntry.second;

        QVector<ActionSchema> inCategory;
        for (const ActionSchema &s : schemas)
        {
            if (s.category == categoryKey)
            {
                inCategory.append(s);
            }
        }
        if (inCategory.isEmpty())
        {
            continue; // No registered op currently uses this category; skip its header entirely rather than printing an empty section.
        }

        out += QStringLiteral("\n=== %1 ===\n").arg(categoryTitle.toUpper());

        for (const ActionSchema &s : inCategory)
        {
            out += QStringLiteral("\n%1%2\n").arg(s.op, s.partial ? QStringLiteral(" [partial]") : QString());
            out += QStringLiteral("    %1\n").arg(s.description);
            if (s.partial)
            {
                out += QStringLiteral("    [partial] %1\n").arg(s.partialReason);
            }
            appendParamTable(out, s.parameters);
            out += QStringLiteral("    Example:\n");
            out += QStringLiteral("        %1\n").arg(s.exampleRequest);
        }
    }

    return out;
}

QJsonArray ToolCatalog::renderAi(const ActionRegistry &registry)
{
    const QVector<ActionSchema> schemas = registry.allSchemas(); // Already sorted alphabetically by op name.

    QJsonArray result;
    for (const ActionSchema &s : schemas)
    {
        QJsonObject properties;
        QJsonArray required;
        for (const ActionParamSchema &p : s.parameters)
        {
            properties[p.name] = paramToJsonSchemaProperty(p);
            if (p.required)
            {
                required.append(p.name);
            }
        }

        QJsonObject inputSchema;
        inputSchema[QStringLiteral("type")] = QStringLiteral("object");
        inputSchema[QStringLiteral("properties")] = properties;
        inputSchema[QStringLiteral("required")] = required;

        QJsonObject entry;
        entry[QStringLiteral("name")] = s.op;
        entry[QStringLiteral("description")] = s.description;
        entry[QStringLiteral("category")] = s.category; // Extra field beyond the minimal {"name","description","input_schema"} shape; harmless for a tool-use API caller, useful for grouping.
        entry[QStringLiteral("input_schema")] = inputSchema;
        if (s.partial) // Every op is always listed (no silent gaps) -- this just flags the two with a documented behavioral gap so an automated caller can choose to avoid them.
        {
            entry[QStringLiteral("status")] = QStringLiteral("partial");
            entry[QStringLiteral("statusReason")] = s.partialReason;
        }

        result.append(entry);
    }

    return result;
}
