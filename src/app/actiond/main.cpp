//---------------------------------------------------------------------------------------------------------------------
//  @file   main.cpp
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

#include "actiond_application.h" // Brings in ActiondApplication, the qApp singleton this process needs.
#include "action_host.h"          // Brings in ActionHost::runActions(), the one-shot (--actions <file>) load-and-run pipeline.
#include "pattern_session.h"      // Brings in PatternSession, the persistent-daemon-mode pattern state.
#include "session_server.h"       // Brings in SessionServer::run(), the persistent-daemon-mode NDJSON loop.

#include "../../libs/ifc/exception/vexception.h" // Brings in VException, the type every reused Seamly2D loading call throws on failure.

#include <QCommandLineOption>  // Provides QCommandLineOption, used to declare every --flag below.
#include <QCommandLineParser>  // Provides QCommandLineParser, used to parse and validate argv against them.
#include <QDir>                // Provides QDir::mkpath()/setCurrent(), used to prepare --output-dir for daemon mode.
#include <QFile>               // Provides QFile, used to read the one-shot mode's actions file and to wrap stdin/stdout for daemon mode.
#include <QJsonDocument>       // Provides QJsonDocument, both the parsed actions script and the printed result type.
#include <QJsonObject>         // Provides QJsonObject, used to build the {"error": "..."} failure payload.
#include <QJsonParseError>     // Provides QJsonParseError, used to detect and report malformed actions JSON.
#include <QScopedPointer>      // Provides QScopedPointer, giving daemon mode's PatternSession RAII cleanup without a manual delete.
#include <QTextStream>         // Provides QTextStream, used to write to stdout/stderr without extra platform-specific code.

namespace
{
    // Writes a single-line {"error": "<message>"} JSON object to stderr, matching the task's
    // required failure-reporting shape for every non-zero exit path below.
    void reportError(const QString &message)
    {
        QJsonObject error;                          // Builds the required {"error": ...} shape.
        error[QStringLiteral("error")] = message;    // The one required field: a human-readable failure reason.
        const QJsonDocument document(error);          // Wraps the object so it can be serialized below.
        QTextStream(stderr) << QString::fromUtf8(document.toJson(QJsonDocument::Compact)) << Qt::endl; // Single compact line, to stderr.
    }
}

// Entry point: forces headless rendering, parses/validates arguments, then delegates to one of two
// modes:
//   - One-shot (--actions <file> given): load a pattern once, run exactly one script, optionally
//     save, print the result, exit. This is the original actiond behavior; --measurements is now
//     optional (it never was before this change), everything else about this mode is unchanged.
//   - Persistent NDJSON daemon (--actions omitted): both --pattern and --measurements become
//     optional (a missing --pattern starts from an empty pattern -- see PatternSession::
//     createEmpty()), --output-dir controls where relative render.snapshot/session.save paths
//     land, and the process reads/dispatches/responds to one NDJSON request per stdin line until
//     EOF or a "session.close" action (see session_server.h for the protocol).
// Every Seamly2D exception is caught here so none can escape main() and terminate the process
// without the required JSON error on stderr.
int main(int argc, char *argv[])
{
    // Registers the "schema" resource (XSD files for validating pattern/measurement XML) that ifc.lib
    // compiles in. Qt's static-lib resource registration needs this explicit call at the consuming
    // binary's own entry point -- ifc.lib's global initializer for it doesn't run on its own when
    // linked into a different executable, exactly as qttestmainlambda.cpp does for Seamly2DTests.
    Q_INIT_RESOURCE(schema);

    // QT_QPA_PLATFORM must be set before QApplication's constructor runs, since that is where Qt
    // resolves which platform plugin to load. Checking qEnvironmentVariableIsSet() first means an
    // explicit choice the caller already made (e.g. a real platform for local debugging) is never
    // silently overridden -- only an *unset* variable gets defaulted to offscreen.
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
    {
        qputenv("QT_QPA_PLATFORM", "offscreen"); // Default to headless so actiond works in CI/containers with no display.
    }

    ActiondApplication app(argc, argv); // Must exist (and be the QCoreApplication::instance()) before any pattern loading below.

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "Runs Seamly2D pattern actions headlessly. With --actions <file>, loads a pattern, runs "
        "that one script, optionally saves, prints the JSON result, and exits (one-shot mode). "
        "Without --actions, starts a persistent daemon that reads one JSON action-batch request "
        "per line from stdin and writes one JSON response per line to stdout, until EOF or a "
        "\"session.close\" action."));
    parser.addHelpOption(); // Standard -h/--help handling, provided by QCommandLineParser itself.

    const QCommandLineOption patternOption(QStringLiteral("pattern"),
        QStringLiteral("Pattern file (.val). Required in one-shot mode; optional in daemon mode (omit to start from an empty pattern)."),
        QStringLiteral("path"));
    const QCommandLineOption measurementsOption(QStringLiteral("measurements"),
        QStringLiteral("Measurements file (.smis/.smms/.vst). Optional in both modes."), QStringLiteral("path"));
    const QCommandLineOption actionsOption(QStringLiteral("actions"),
        QStringLiteral("Actions JSON file. Given: run once and exit (one-shot mode). Omitted: start the persistent NDJSON daemon instead."),
        QStringLiteral("path"));
    const QCommandLineOption savePatternOption(QStringLiteral("save-pattern"),
        QStringLiteral("One-shot mode only: write the (possibly mutated) pattern here after running the script."), QStringLiteral("path"));
    const QCommandLineOption outputDirOption(QStringLiteral("output-dir"),
        QStringLiteral("Daemon mode only: directory relative paths in render.snapshot/session.save actions resolve against. Created if missing."),
        QStringLiteral("dir"), QStringLiteral("./output"));
    parser.addOption(patternOption);
    parser.addOption(measurementsOption);
    parser.addOption(actionsOption);
    parser.addOption(savePatternOption);
    parser.addOption(outputDirOption);
    parser.process(app); // Parses argv; also handles --help/--version and unknown-option errors itself.

    const bool oneShotMode = parser.isSet(actionsOption);

    // One-shot mode still requires --pattern (unchanged from before this change); daemon mode
    // requires neither --pattern nor --measurements.
    if (oneShotMode && !parser.isSet(patternOption))
    {
        QTextStream(stderr) << QStringLiteral(
            "Usage: actiond --pattern <path.val> --actions <path.json> [--measurements <path>] [--save-pattern <path>]")
            << Qt::endl;
        return 2; // Distinct from the "1" runtime-failure exit code below, so callers can tell usage errors apart from load/action failures.
    }

    const QString patternPath = parser.value(patternOption);           // Empty if not given -- valid in daemon mode, checked above for one-shot mode.
    const QString measurementsPath = parser.value(measurementsOption); // Empty if not given -- optional in both modes.

    try
    {
        if (oneShotMode)
        {
            const QString actionsPath = parser.value(actionsOption);
            const QString savePatternPath = parser.value(savePatternOption); // Empty string ("") if --save-pattern was not given; ActionHost::runActions() treats that as "don't save".

            QFile actionsFile(actionsPath); // The actions script file named on the command line.
            if (!actionsFile.open(QIODevice::ReadOnly | QIODevice::Text)) // Covers both "missing" and "unreadable" in one check.
            {
                throw VException(QStringLiteral("Cannot open actions file: %1").arg(actionsPath)); // Same exception type/shape as every load failure below.
            }
            const QByteArray actionsBytes = actionsFile.readAll(); // Whole file at once; action scripts are expected to be small.
            actionsFile.close();                                    // Release the file handle promptly; nothing else reads it.

            QJsonParseError parseError; // Populated by QJsonDocument::fromJson() below on malformed input.
            const QJsonDocument actionsScript = QJsonDocument::fromJson(actionsBytes, &parseError); // Parses the raw bytes as JSON.
            if (parseError.error != QJsonParseError::NoError) // Malformed JSON is a user input error, reported the same way as any other failure.
            {
                throw VException(QStringLiteral("Invalid actions JSON (%1): %2").arg(parseError.errorString(), actionsPath));
            }

            // The actual work: load the pattern (+ optional measurements), run the script, get the
            // result back. savePatternPath is empty unless --save-pattern was given.
            const QJsonDocument result = ActionHost::runActions(patternPath, measurementsPath, actionsScript, savePatternPath);

            // Success: the ActionEngine result, printed as a single compact JSON line on stdout.
            QTextStream(stdout) << QString::fromUtf8(result.toJson(QJsonDocument::Compact)) << Qt::endl;
            return 0;
        }

        // ---- Persistent NDJSON daemon mode (--actions omitted) ----

        const QString outputDir = parser.value(outputDirOption); // Defaults to "./output" (set as the option's own default value above).
        if (!QDir().mkpath(outputDir))
        {
            throw VException(QStringLiteral("Cannot create output directory: %1").arg(outputDir));
        }
        if (!QDir::setCurrent(outputDir)) // Every relative path a render.snapshot/session.save action names is resolved against the process's current directory -- see those handlers' own QFileInfo::absolutePath() calls -- so setting it here once, at startup, is all that's needed; neither handler needs to know about --output-dir itself.
        {
            throw VException(QStringLiteral("Cannot switch to output directory: %1").arg(outputDir));
        }

        // A missing --pattern starts from an empty pattern (PatternSession::createEmpty()); both
        // factories throw VException on failure, caught by this function's own catch clauses below.
        QScopedPointer<PatternSession> session(patternPath.isEmpty()
            ? PatternSession::createEmpty(measurementsPath)
            : PatternSession::loadFromFile(patternPath, measurementsPath));

        // Wraps the process's real stdin/stdout as QIODevices for SessionServer::run(). Diagnostic
        // output (qDebug/qWarning, any startup banner) must never be written here -- stdout is
        // reserved exclusively for the NDJSON protocol; this process emits none besides that.
        QFile input;
        input.open(stdin, QIODevice::ReadOnly);
        QFile output;
        output.open(stdout, QIODevice::WriteOnly);

        SessionServer::run(*session, &input, &output); // Blocks until EOF or a "session.close" action; never throws (see its own header comment).
        return 0;
    }
    catch (const VException &exception) // Every reused Seamly2D loading call (setXMLContent, Parse, readMeasurements, ...) throws this.
    {
        reportError(exception.ErrorMessage()); // Seamly2D's own human-readable message for the failure.
        return 1;
    }
    catch (const std::exception &exception) // Catches anything else well-behaved (e.g. a standard library exception) that isn't a VException.
    {
        reportError(QString::fromUtf8(exception.what())); // std::exception's own message, best-effort.
        return 1;
    }
    catch (...) // Absolute last resort: guarantees no exception of any kind escapes main() uncaught, per the task's hard requirement.
    {
        reportError(QStringLiteral("Unknown error")); // No further detail is available for a non-std::exception throw.
        return 1;
    }
}
