//---------------------------------------------------------------------------------------------------------------------
//  @file   main.cpp
//  @author Seamly2D Contributors
//  @date   20 Aug, 2026
//
//  @brief
//  run_batch: the C++ replacement for tests/actionlayer/run_batch.py + run_tests.py + friends.
//
//  Usage:
//      run_batch                                            (no args: runs every scripts/*.json
//                                                             case against the default fixtures;
//                                                             this is what 'make check' invokes)
//      run_batch <actions.json>
//      run_batch <pattern.val> <actions.json>
//      run_batch <pattern.val> <measurements> <actions.json>
//      ... any of the above, plus --update at the end
//
//  Positional arguments are recognized by file extension, not by position, so
//  "run_batch fixtures/measurements/sample.smis fixtures/patterns/minimal.val scripts/foo.json"
//  works exactly like the reverse order would. Exactly one .json argument (the actions script) is
//  required whenever any positional argument is given at all.
//
//  Without --update: runs the case (or every case, in no-args mode), diffs actiond's JSON response
//  against tests/actionlayer/expected/<case>.expected.json, and exits 0 only if every case's
//  response matches its golden file exactly. Any render.snapshot PNG the case produces is checked
//  for existence only (never byte-diffed -- PNG encoding is not guaranteed stable across Qt/platform
//  versions; see README.md's "Interpreting a failure" section).
//
//  With --update (single-case mode only): runs the one named case, and overwrites its golden
//  files (expected/<case>.expected.json, and expected/<case>.png if the case renders exactly one
//  image) with whatever actiond just produced, instead of diffing. Prints what changed. This does
//  NOT review whether the new output is *correct* -- see README.md's "Adding a new test case"
//  section for why a human diff review is required after every --update before committing.
//
//  Exit codes (matching run_batch.py's existing contract, so CI callers do not need to change):
//      0 -- every case ran to completion and matched its golden file (or --update succeeded).
//      1 -- every case ran to completion, but at least one did not match its golden file.
//      2 -- a case's actiond process could not be found/run/parsed at all (crash, usage error,
//           missing golden file with no --update, missing actiond.exe, ...).
//---------------------------------------------------------------------------------------------------------------------

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QProcess>
#include <QStringList>
#include <QTextStream>
#include <QDebug>

namespace
{
    QTextStream out(stdout);
    QTextStream err(stderr);

    // ACTIONLAYER_TESTS_DIR is injected by run_batch.pro as a DEFINES value that is already a
    // fully quoted C string literal (see the \\\" escaping in run_batch.pro), so it is used
    // directly here -- no stringizing macro needed, unlike a raw unquoted token would.
    const QString kTestsDir = QDir::cleanPath(QString::fromUtf8(ACTIONLAYER_TESTS_DIR));

    QString defaultPattern()     { return kTestsDir + QStringLiteral("/fixtures/patterns/blank.val"); }
    QString defaultMeasurements(){ return kTestsDir + QStringLiteral("/fixtures/measurements/sample.smis"); }
    QString scriptsDir()         { return kTestsDir + QStringLiteral("/scripts"); }
    QString expectedDir()        { return kTestsDir + QStringLiteral("/expected"); }
    QString outputDir()          { return kTestsDir + QStringLiteral("/output"); }

    bool isMeasurementsFile(const QString &path)
    {
        const QString suffix = QFileInfo(path).suffix().toLower();
        return suffix == QLatin1String("smis") || suffix == QLatin1String("smms") || suffix == QLatin1String("vst");
    }

    // Finds actiond.exe (or actiond on non-Windows): $ACTIOND_EXE if set, else walk upward from
    // this binary's own directory looking for an ancestor literally named "out" (the qmake build
    // output root every .pro in this repo builds into -- see build_actiond.bat), then descend into
    // src/app/actiond/bin from there. Robust to run_batch.exe itself landing at any depth under
    // out/, since it does not hardcode a fixed number of ".." steps.
    QString findActiond()
    {
        const QString override = qEnvironmentVariable("ACTIOND_EXE");
        if (!override.isEmpty())
        {
            return override;
        }

#if defined(Q_OS_WIN)
        const QString exeName = QStringLiteral("actiond.exe");
#else
        const QString exeName = QStringLiteral("actiond");
#endif

        QDir dir(QCoreApplication::applicationDirPath());
        while (true)
        {
            if (dir.dirName() == QLatin1String("out"))
            {
                const QString candidate = dir.filePath(QStringLiteral("src/app/actiond/bin/") + exeName);
                if (QFileInfo::exists(candidate))
                {
                    return candidate;
                }
                break;
            }
            if (!dir.cdUp())
            {
                break;
            }
        }
        return QString(); // Not found; caller reports a clear error.
    }

    // Recursively compares two QJsonValues, appending a short human-readable description of each
    // mismatch (up to maxDiffs) to diffsOut. Object key order never matters (QJsonObject compares
    // as a map); array order and length do matter (a script's actions run in a fixed order).
    void diffJson(const QJsonValue &expected, const QJsonValue &actual, const QString &path,
                  QStringList &diffsOut, int maxDiffs)
    {
        if (diffsOut.size() >= maxDiffs)
        {
            return;
        }
        if (expected.type() != actual.type())
        {
            diffsOut << QStringLiteral("%1: type mismatch (expected %2, got %3)")
                            .arg(path).arg(int(expected.type())).arg(int(actual.type()));
            return;
        }
        switch (expected.type())
        {
            case QJsonValue::Object:
            {
                const QJsonObject eo = expected.toObject();
                const QJsonObject ao = actual.toObject();
                for (auto it = eo.constBegin(); it != eo.constEnd(); ++it)
                {
                    const QString childPath = path + QLatin1Char('.') + it.key();
                    if (!ao.contains(it.key()))
                    {
                        diffsOut << QStringLiteral("%1: missing in actual").arg(childPath);
                        if (diffsOut.size() >= maxDiffs) return;
                        continue;
                    }
                    diffJson(it.value(), ao.value(it.key()), childPath, diffsOut, maxDiffs);
                    if (diffsOut.size() >= maxDiffs) return;
                }
                for (auto it = ao.constBegin(); it != ao.constEnd(); ++it)
                {
                    if (!eo.contains(it.key()))
                    {
                        diffsOut << QStringLiteral("%1.%2: unexpected key in actual").arg(path, it.key());
                        if (diffsOut.size() >= maxDiffs) return;
                    }
                }
                break;
            }
            case QJsonValue::Array:
            {
                const QJsonArray ea = expected.toArray();
                const QJsonArray aa = actual.toArray();
                if (ea.size() != aa.size())
                {
                    diffsOut << QStringLiteral("%1: array length mismatch (expected %2, got %3)")
                                    .arg(path).arg(ea.size()).arg(aa.size());
                    return;
                }
                for (int i = 0; i < ea.size(); ++i)
                {
                    diffJson(ea.at(i), aa.at(i), QStringLiteral("%1[%2]").arg(path).arg(i), diffsOut, maxDiffs);
                    if (diffsOut.size() >= maxDiffs) return;
                }
                break;
            }
            default:
                if (expected != actual)
                {
                    diffsOut << QStringLiteral("%1: expected %2, got %3")
                                    .arg(path,
                                         QString::fromUtf8(QJsonDocument(QJsonArray{expected}).toJson(QJsonDocument::Compact)),
                                         QString::fromUtf8(QJsonDocument(QJsonArray{actual}).toJson(QJsonDocument::Compact)));
                }
                break;
        }
    }

    QJsonDocument prettyRead(const QString &path, bool *ok)
    {
        QFile f(path);
        *ok = f.open(QIODevice::ReadOnly | QIODevice::Text);
        if (!*ok)
        {
            return QJsonDocument();
        }
        QJsonParseError perr;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
        *ok = (perr.error == QJsonParseError::NoError);
        return doc;
    }

    bool writeFile(const QString &path, const QByteArray &bytes)
    {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            return false;
        }
        f.write(bytes);
        return true;
    }

    // Result of running one case, independent of pass/fail-against-golden -- exitStatus follows
    // actiond's own 0/1/2/crash contract (see module header above and main.cpp in src/app/actiond).
    struct RunResult
    {
        bool   ranAtAll = false; // false only if actiond.exe itself could not be started.
        int    exitCode = -1;
        QJsonDocument response;  // Valid whenever ranAtAll && exitCode <= 1.
        QStringList pngFiles;    // Basenames of every *.png the case's own output directory now holds.
    };

    RunResult runOneCase(const QString &actiondExe, const QString &pattern, const QString &measurements,
                          const QString &actionsPath, const QString &caseOutputDir)
    {
        RunResult result;
        QDir().mkpath(caseOutputDir);

        const QString savePatternPath = caseOutputDir + QStringLiteral("/pattern.val");

        // actiond runs with its working directory set to caseOutputDir (below), which is not the
        // directory run_batch itself was invoked from -- so every path handed to it on the command
        // line must be absolute, or a relative "--pattern"/"--actions" would resolve against the
        // wrong directory (a real bug hit and fixed while building this harness: actiond reported
        // "Cannot open actions file" because the relative path it was given resolved against
        // caseOutputDir instead of run_batch's own cwd). Only a script's own *internal* "path"
        // fields (e.g. render.snapshot's "path") are meant to resolve against caseOutputDir.
        QStringList args;
        args << QStringLiteral("--pattern") << QFileInfo(pattern).absoluteFilePath()
             << QStringLiteral("--actions") << QFileInfo(actionsPath).absoluteFilePath()
             << QStringLiteral("--save-pattern") << savePatternPath;
        if (!measurements.isEmpty())
        {
            args << QStringLiteral("--measurements") << QFileInfo(measurements).absoluteFilePath();
        }

        QProcess proc;
        proc.setWorkingDirectory(caseOutputDir); // Any relative "path" a render.snapshot/etc. action names lands here directly.
        proc.start(actiondExe, args);
        if (!proc.waitForStarted(10000))
        {
            result.ranAtAll = false;
            err << QStringLiteral("    QProcess::start failed: %1\n").arg(proc.errorString());
            return result;
        }
        proc.waitForFinished(60000);
        result.ranAtAll = true;
        result.exitCode = proc.exitCode();

        const QByteArray stdoutBytes = proc.readAllStandardOutput();
        const QByteArray stderrBytes = proc.readAllStandardError();
        writeFile(caseOutputDir + QStringLiteral("/stderr.log"), stderrBytes);

        QJsonObject responseObj;
        if (result.exitCode == 0)
        {
            QJsonParseError perr;
            result.response = QJsonDocument::fromJson(stdoutBytes, &perr);
            if (perr.error != QJsonParseError::NoError)
            {
                responseObj[QStringLiteral("error")] = QStringLiteral("actiond exited 0 but stdout was not valid JSON: %1").arg(perr.errorString());
                result.response = QJsonDocument(responseObj);
                result.exitCode = -1; // Sentinel: "ran, but response is unusable" -- distinct from every real actiond exit code.
            }
        }
        else if (result.exitCode == 1)
        {
            QJsonParseError perr;
            const QJsonDocument errDoc = QJsonDocument::fromJson(stderrBytes.trimmed(), &perr);
            if (perr.error == QJsonParseError::NoError)
            {
                result.response = errDoc;
            }
            else
            {
                responseObj[QStringLiteral("error")] = QString::fromUtf8(stderrBytes.trimmed());
                result.response = QJsonDocument(responseObj);
            }
        }
        // exitCode == 2 (usage error) or >2 (crash): result.response left null; caller reports via exitCode.

        writeFile(caseOutputDir + QStringLiteral("/response.json"), result.response.toJson(QJsonDocument::Indented));

        QDir dir(caseOutputDir);
        for (const QFileInfo &fi : dir.entryInfoList(QStringList() << QStringLiteral("*.png"), QDir::Files))
        {
            result.pngFiles << fi.fileName();
        }
        return result;
    }

    // Runs one case and either updates its golden files or diffs against them. Returns true if the
    // case passed (or --update succeeded); prints its own PASS/FAIL/ERROR line either way.
    bool processCase(const QString &actiondExe, const QString &pattern, const QString &measurements,
                      const QString &actionsPath, bool updateMode)
    {
        const QString caseName = QFileInfo(actionsPath).completeBaseName();
        const QString caseOutputDir = outputDir() + QLatin1Char('/') + caseName;
        const QString expectedJsonPath = expectedDir() + QLatin1Char('/') + caseName + QStringLiteral(".expected.json");

        if (!QFileInfo::exists(actionsPath))
        {
            err << QStringLiteral("[%1] ERROR: actions file not found: %2\n").arg(caseName, actionsPath);
            return false;
        }

        const RunResult result = runOneCase(actiondExe, pattern, measurements, actionsPath, caseOutputDir);

        if (!result.ranAtAll)
        {
            err << QStringLiteral("[%1] ERROR: could not start actiond at %2\n").arg(caseName, actiondExe);
            return false;
        }
        if (result.exitCode > 2 || result.exitCode < 0)
        {
            err << QStringLiteral("[%1] CRASHED: actiond exited with code %2 -- see %3/stderr.log\n")
                       .arg(caseName).arg(result.exitCode).arg(caseOutputDir);
            return false;
        }
        if (result.exitCode == 2)
        {
            err << QStringLiteral("[%1] ERROR: actiond reported a usage error -- see %2/stderr.log\n").arg(caseName, caseOutputDir);
            return false;
        }
        if (result.exitCode == 1)
        {
            // A clean top-level failure (e.g. the pattern/measurements file itself failed to
            // load) -- none of this suite's fixtures are expected to ever hit this, so treat it
            // as a hard error rather than something --update should silently record as golden.
            err << QStringLiteral("[%1] ERROR: actiond failed to run the batch at all -- %2\n")
                       .arg(caseName, QString::fromUtf8(result.response.toJson(QJsonDocument::Compact)));
            return false;
        }

        if (updateMode)
        {
            writeFile(expectedJsonPath, result.response.toJson(QJsonDocument::Indented));
            out << QStringLiteral("[%1] UPDATED golden: %2\n").arg(caseName, expectedJsonPath);

            if (result.pngFiles.size() == 1)
            {
                const QString src = caseOutputDir + QLatin1Char('/') + result.pngFiles.first();
                const QString dst = expectedDir() + QLatin1Char('/') + caseName + QStringLiteral(".png");
                QFile::remove(dst);
                QFile::copy(src, dst);
                out << QStringLiteral("[%1] UPDATED golden: %2\n").arg(caseName, dst);
            }
            else
            {
                for (const QString &png : result.pngFiles)
                {
                    const QString src = caseOutputDir + QLatin1Char('/') + png;
                    const QString dst = expectedDir() + QLatin1Char('/') + caseName + QStringLiteral("__") + png;
                    QFile::remove(dst);
                    QFile::copy(src, dst);
                    out << QStringLiteral("[%1] UPDATED golden: %2\n").arg(caseName, dst);
                }
            }
            return true;
        }

        // Diff mode.
        bool expectedOk = false;
        const QJsonDocument expectedDoc = prettyRead(expectedJsonPath, &expectedOk);
        if (!expectedOk)
        {
            err << QStringLiteral("[%1] ERROR: no golden file at %2 -- run with --update once, review the diff, then commit it\n")
                       .arg(caseName, expectedJsonPath);
            return false;
        }

        QStringList diffs;
        const QJsonValue expectedValue = expectedDoc.isArray() ? QJsonValue(expectedDoc.array()) : QJsonValue(expectedDoc.object());
        const QJsonValue actualValue = result.response.isArray() ? QJsonValue(result.response.array()) : QJsonValue(result.response.object());
        diffJson(expectedValue, actualValue, QStringLiteral("$"), diffs, 20);

        // PNG existence check (never byte-diffed -- see module header).
        const QFileInfo expectedPng(expectedDir() + QLatin1Char('/') + caseName + QStringLiteral(".png"));
        if (expectedPng.exists() && result.pngFiles.isEmpty())
        {
            diffs << QStringLiteral("$: expected a rendered PNG (%1) but the case produced none").arg(expectedPng.fileName());
        }

        if (diffs.isEmpty())
        {
            out << QStringLiteral("[%1] PASS\n").arg(caseName);
            return true;
        }

        err << QStringLiteral("[%1] FAIL (%2 mismatch(es)):\n").arg(caseName).arg(diffs.size());
        for (const QString &d : diffs)
        {
            err << QStringLiteral("    %1\n").arg(d);
        }
        err << QStringLiteral("    actual response: %1/response.json\n").arg(caseOutputDir);
        return false;
    }

    // Regression check for the `actiond --list-tools` command (action_registry.cpp's
    // registerBuiltinActions()/tool_catalog.cpp/src/app/actiond/main.cpp's --list-tools handling):
    // spawns `actiond --list-tools --format=ai` directly (no --pattern/--actions involved -- this
    // command is pure introspection over ActionRegistry) and checks its stdout is valid, parseable
    // JSON forming a non-empty array, with nothing else mixed into stdout. This is the CLI-surface
    // half of that command's drift-detection coverage; the complementary "does the entry count match
    // ActionRegistry::actionCount()" check lives in src/test/ActionLayerTest/tst_action_schema.cpp,
    // which links actionlayer directly and so can compare against the registry's own live count --
    // something this Qt-core-only harness has no way to do (see run_batch.pro's own comment on why
    // it deliberately does not link any Seamly2D lib).
    bool checkListToolsAi(const QString &actiondExe)
    {
        QProcess proc;
        proc.start(actiondExe, QStringList() << QStringLiteral("--list-tools") << QStringLiteral("--format=ai"));
        if (!proc.waitForStarted(10000))
        {
            err << QStringLiteral("[list_tools_ai] ERROR: could not start actiond at %1\n").arg(actiondExe);
            return false;
        }
        proc.waitForFinished(15000);

        if (proc.exitCode() != 0)
        {
            err << QStringLiteral("[list_tools_ai] FAIL: actiond --list-tools --format=ai exited %1 -- stderr: %2\n")
                       .arg(proc.exitCode()).arg(QString::fromUtf8(proc.readAllStandardError().trimmed()));
            return false;
        }

        const QByteArray stdoutBytes = proc.readAllStandardOutput();
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(stdoutBytes, &parseError);
        if (parseError.error != QJsonParseError::NoError)
        {
            err << QStringLiteral("[list_tools_ai] FAIL: stdout was not valid JSON: %1\n").arg(parseError.errorString());
            return false;
        }
        if (!document.isArray())
        {
            err << QStringLiteral("[list_tools_ai] FAIL: top-level JSON value must be an array\n");
            return false;
        }
        const QJsonArray tools = document.array();
        if (tools.isEmpty())
        {
            err << QStringLiteral("[list_tools_ai] FAIL: tools array must not be empty\n");
            return false;
        }
        for (const QJsonValue &value : tools)
        {
            if (!value.isObject() || value.toObject().value(QStringLiteral("name")).toString().isEmpty()
                || value.toObject().value(QStringLiteral("description")).toString().isEmpty()
                || !value.toObject().value(QStringLiteral("input_schema")).isObject())
            {
                err << QStringLiteral("[list_tools_ai] FAIL: an entry is missing the minimal {\"name\",\"description\",\"input_schema\"} shape\n");
                return false;
            }
        }

        out << QStringLiteral("[list_tools_ai] PASS (%1 tools)\n").arg(tools.size());
        return true;
    }
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QStringList args = app.arguments().mid(1);
    const bool updateMode = args.removeAll(QStringLiteral("--update")) > 0;

    QDir().mkpath(outputDir());
    QDir().mkpath(expectedDir());

    const QString actiondExe = findActiond();
    if (actiondExe.isEmpty())
    {
        err << QStringLiteral("ERROR: could not find actiond.exe. Build it first (build_actiond.bat at the "
                               "repo root), or set the ACTIOND_EXE environment variable.\n");
        return 2;
    }

    if (args.isEmpty())
    {
        // No-args mode: run every scripts/*.json case against the default fixtures. This is what
        // 'make check' invokes (CONFIG += testcase in run_batch.pro runs this binary with no args).
        if (updateMode)
        {
            err << QStringLiteral("ERROR: --update requires a specific case (run_batch <actions.json> --update); "
                                   "bulk-updating every golden file at once is not supported on purpose -- each "
                                   "one needs its own human review before committing (see README.md).\n");
            return 2;
        }

        QDir dir(scriptsDir());
        const QFileInfoList scripts = dir.entryInfoList(QStringList() << QStringLiteral("*.json"), QDir::Files, QDir::Name);
        if (scripts.isEmpty())
        {
            err << QStringLiteral("ERROR: no *.json scripts found in %1\n").arg(scriptsDir());
            return 2;
        }

        int passed = 0;
        for (const QFileInfo &fi : scripts)
        {
            if (processCase(actiondExe, defaultPattern(), defaultMeasurements(), fi.absoluteFilePath(), false))
            {
                ++passed;
            }
        }

        const int totalChecks = scripts.size() + 1; // +1 for checkListToolsAi() below, an actiond-CLI check independent of any scripts/*.json case.
        if (checkListToolsAi(actiondExe))
        {
            ++passed;
        }

        out << QStringLiteral("%1/%2 case(s) passed.\n").arg(passed).arg(totalChecks);
        return passed == totalChecks ? 0 : 1;
    }

    // Single-case mode: classify 1-3 positional args by extension.
    QString pattern, measurements, actionsPath;
    for (const QString &a : args)
    {
        const QString suffix = QFileInfo(a).suffix().toLower();
        if (suffix == QLatin1String("json"))
        {
            actionsPath = a;
        }
        else if (suffix == QLatin1String("val"))
        {
            pattern = a;
        }
        else if (isMeasurementsFile(a))
        {
            measurements = a;
        }
        else
        {
            err << QStringLiteral("ERROR: don't know how to classify argument (expected .val/.smis/.smms/.vst/.json): %1\n").arg(a);
            return 2;
        }
    }
    if (actionsPath.isEmpty())
    {
        err << QStringLiteral("ERROR: no actions .json file given.\n"
                               "Usage: run_batch [<pattern.val>] [<measurements>] <actions.json> [--update]\n");
        return 2;
    }
    if (pattern.isEmpty())
    {
        pattern = defaultPattern();
    }
    if (measurements.isEmpty())
    {
        measurements = defaultMeasurements();
    }

    const bool passed = processCase(actiondExe, pattern, measurements, actionsPath, updateMode);
    return passed ? 0 : 1;
}
