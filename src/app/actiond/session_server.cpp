//---------------------------------------------------------------------------------------------------------------------
//  @file   session_server.cpp
//  @author Seamly2D Contributors
//  @date   20 Aug, 2026
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

#include "session_server.h"  // Brings in the SessionServer::run declaration this file implements.
#include "pattern_session.h" // Brings in PatternSession::runActions(), this loop's real work per request line.

#include "../../libs/ifc/exception/vexception.h" // Brings in VException, the type every reused Seamly2D loading/save call can throw -- caught below so one bad request never crashes the persistent process.

#include <QIODevice>       // Provides QIODevice, the abstract type run()'s input/output parameters use.
#include <QJsonArray>      // Provides QJsonArray, used for "actions" (input) and "results" (output).
#include <QJsonDocument>   // Provides QJsonDocument, used to parse each request line and serialize each response line.
#include <QJsonObject>     // Provides QJsonObject, the shape of both the request and the response.
#include <QJsonParseError> // Provides QJsonParseError, used to detect and report malformed request JSON.
#include <QJsonValue>      // Provides QJsonValue, used for the nullable "id"/"result"/"error" fields.
#include <QTextStream>     // Provides QTextStream, used for line-buffered reads/writes over the given QIODevices.

namespace
{
    // Builds a top-level error response: {"id","status":"error","appliedCount":0,"results":[],
    // "error"}. Used for every failure mode that happens *outside* per-action dispatch (a
    // malformed request line, or an exception PatternSession::runActions() itself let escape) --
    // as opposed to a per-action failure, which is reported inside "results" instead, with the
    // top-level "status" staying "ok"/"partial" and top-level "error" staying null.
    QJsonObject topLevelError(const QJsonValue &id, const QString &message)
    {
        QJsonObject response;
        response["id"] = id;
        response["status"] = QStringLiteral("error");
        response["appliedCount"] = 0;
        response["results"] = QJsonArray();
        response["error"] = message;
        return response;
    }

    // Converts one ActionEngine-shaped result {"op","ok","value","error"} (see action_engine.cpp's
    // own toJson()) into this protocol's documented per-result shape
    // {"op","index","status":"ok"|"error","result","error"}.
    QJsonObject remapResult(const QJsonObject &raw, int index)
    {
        const bool ok = raw.value(QStringLiteral("ok")).toBool();

        QJsonObject entry;
        entry["op"] = raw.value(QStringLiteral("op"));
        entry["index"] = index;
        entry["status"] = ok ? QStringLiteral("ok") : QStringLiteral("error");
        entry["result"] = ok ? raw.value(QStringLiteral("value")) : QJsonValue();
        entry["error"] = ok ? QJsonValue() : raw.value(QStringLiteral("error"));
        return entry;
    }

    // Processes one request line end to end and returns the single compact-JSON response line to
    // write back. Sets shouldExit to true if a "session.close" action was among the results
    // actually reached (bounded by how far an "abort" onError let dispatch get).
    QString processLine(const QString &line, PatternSession &session, bool &shouldExit)
    {
        QJsonParseError parseError;
        const QJsonDocument requestDoc = QJsonDocument::fromJson(line.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !requestDoc.isObject())
        {
            return QString::fromUtf8(QJsonDocument(topLevelError(QJsonValue(),
                QStringLiteral("Invalid JSON request: %1").arg(parseError.errorString())))
                .toJson(QJsonDocument::Compact));
        }

        const QJsonObject request = requestDoc.object();
        const QJsonValue id = request.contains(QStringLiteral("id")) ? request.value(QStringLiteral("id")) : QJsonValue();

        if (!request.contains(QStringLiteral("actions")) || !request.value(QStringLiteral("actions")).isArray())
        {
            return QString::fromUtf8(QJsonDocument(topLevelError(id,
                QStringLiteral("Request must have an \"actions\" array"))).toJson(QJsonDocument::Compact));
        }

        // Anything other than the literal "continue" aborts on the first failure -- matches the
        // documented default ("abort") and treats an unrecognized value the same way, rather than
        // silently guessing "continue" for a caller's typo.
        const QString onError = request.value(QStringLiteral("onError")).toString(QStringLiteral("abort"));
        const bool abortOnFirstError = (onError != QStringLiteral("continue"));

        QJsonArray rawResults;
        try
        {
            const QJsonDocument engineResult = session.runActions(QJsonDocument(request), abortOnFirstError);
            rawResults = engineResult.object().value(QStringLiteral("results")).toArray();
        }
        // Every handler already converts its own reused-Seamly2D-call failures into a per-action
        // ActionResult::failure() (see e.g. formula_point_handlers.cpp's runCreate()), so this
        // clause should rarely fire -- but it is the persistent process's own last-resort net: one
        // request that somehow still throws must become a top-level error response for that one
        // line, not a crash that takes down every request still to come.
        catch (const VException &error)
        {
            return QString::fromUtf8(QJsonDocument(topLevelError(id, error.ErrorMessage())).toJson(QJsonDocument::Compact));
        }
        catch (const std::exception &error)
        {
            return QString::fromUtf8(QJsonDocument(topLevelError(id, QString::fromUtf8(error.what()))).toJson(QJsonDocument::Compact));
        }
        catch (...)
        {
            return QString::fromUtf8(QJsonDocument(topLevelError(id, QStringLiteral("Unknown error running actions"))).toJson(QJsonDocument::Compact));
        }

        QJsonArray results;
        int appliedCount = 0;
        bool anyFailure = false;
        for (int i = 0; i < rawResults.size(); ++i)
        {
            const QJsonObject raw = rawResults.at(i).toObject();
            results.append(remapResult(raw, i));

            if (raw.value(QStringLiteral("ok")).toBool())
            {
                ++appliedCount;
            }
            else
            {
                anyFailure = true;
            }

            if (raw.value(QStringLiteral("op")).toString() == QStringLiteral("session.close"))
            {
                shouldExit = true; // Only actions actually reached (bounded by an "abort") can set this.
            }
        }

        const int inputActionCount = request.value(QStringLiteral("actions")).toArray().size();
        QString status;
        if (!anyFailure && results.size() == inputActionCount)
        {
            status = QStringLiteral("ok"); // Every input action ran and every one succeeded.
        }
        else if (abortOnFirstError)
        {
            status = QStringLiteral("error"); // Stopped early: results.size() < inputActionCount, the last entry being the failure that stopped it.
        }
        else
        {
            status = QStringLiteral("partial"); // onError:"continue": every input action ran, at least one failed.
        }

        QJsonObject response;
        response["id"] = id;
        response["status"] = status;
        response["appliedCount"] = appliedCount;
        response["results"] = results;
        response["error"] = QJsonValue(); // Per-action failures live inside "results"; this stays null on this path by definition.
        return QString::fromUtf8(QJsonDocument(response).toJson(QJsonDocument::Compact));
    }
}

namespace SessionServer
{
    void run(PatternSession &session, QIODevice *input, QIODevice *output)
    {
        QTextStream in(input);
        QTextStream out(output);

        bool shouldExit = false;
        while (!shouldExit)
        {
            const QString line = in.readLine(); // Blocks until a full line is available or the device reaches EOF.
            if (line.isNull()) // QTextStream::readLine() returns a null (not merely empty) QString only at true EOF -- distinct from a genuinely blank input line, which is still dispatched below (and reported as a JSON-parse failure, per the documented protocol).
            {
                break;
            }

            const QString responseLine = processLine(line, session, shouldExit);
            out << responseLine << '\n'; // Single compact line, no embedded newlines (processLine()'s QJsonDocument::Compact serialization guarantees this).
            out.flush(); // The caller is blocking on a readline; buffered output would hang it.
        }
    }
}
