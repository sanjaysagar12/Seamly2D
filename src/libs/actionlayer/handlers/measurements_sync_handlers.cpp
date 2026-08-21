//---------------------------------------------------------------------------------------------------------------------
//  @file   measurements_sync_handlers.cpp
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

#include "measurements_sync_handlers.h" // Brings in the three ActionResult-returning handle* declarations this file implements.

#include "../action_context.h" // Brings in ActionContext, supplying doc/data for every handler below.

#include "../../vpatterndb/vcontainer.h"           // Brings in VContainer: ClearVariables(), setSize()/setHeight(), rsize()/rheight(), DataVariables(), DataMeasurements().
#include "../../vpatterndb/measurements_def.h"     // Brings in size_M/height_M, the individual-file special variable names setSizeHeightForIndividualM() reads.
#include "../../vpatterndb/variables/vinternalvariable.h" // Brings in VInternalVariable::GetValue(), used to read size_M/height_M's current values.
#include "../../vformat/measurements.h"            // Brings in MeasurementDoc, this file's equivalent of MainWindow::openMeasurementFile()'s own core object.
#include "../../ifc/xml/individual_size_converter.h" // Brings in IndividualSizeConverter, upgrading an older-format individual file's schema.
#include "../../ifc/xml/multi_size_converter.h"       // Brings in MultiSizeConverter, upgrading an older-format multisize file's schema.
#include "../../ifc/xml/vabstractpattern.h"        // Brings in VAbstractPattern: the Document enum, ListMeasurements(), LiteParseTree().
#include "../../ifc/exception/vexception.h"        // Brings in VException, thrown by setXMLContent()/readMeasurements()/the converters on a malformed file.
#include "../../qmuparser/qmuparsererror.h"        // Brings in qmu::QmuParserError; see recomputePattern()'s comment for why this handler -- uniquely among the three -- must catch it.
#include "../../vmisc/vabstractapplication.h"      // Brings in the qApp macro: patternType(), used for the type-consistency guard below, and getFilePath(), used by the SetMPath() call below.
#include "../../vmisc/def.h"                       // Brings in MeasurementsType, VarType, and Unit, classified/compared throughout this file, plus RelativeMPath(), used by the SetMPath() call below.

#include <QFileInfo>      // Provides QFileInfo::exists(), the same fast existence check action_host.cpp's requireFileExists() uses.
#include <QHash>          // Provides QHash, the type DataVariables() returns a pointer to.
#include <QJsonArray>     // Provides QJsonArray, used for the "missing" measurement-name list in a missingMeasurements error.
#include <QJsonObject>    // Provides QJsonObject, used for both the input args and every output/error payload.
#include <QJsonValue>     // Provides QJsonValue, the type both a plain-string and a structured-object error are carried as.
#include <QMap>           // Provides QMap, the type DataMeasurements() returns.
#include <QSet>           // Provides QSet, used to diff the pattern's required measurement names against the new file's.
#include <QSharedPointer> // Provides QSharedPointer, the storage type DataVariables()/DataMeasurements() values use.
#include <QString>        // Provides QString, used throughout for field values and messages.
#include <QStringList>    // Provides QStringList, the return type of ListAll()/ListMeasurements().

namespace
{
    // Converts a MeasurementsType enumerator to the lowercase string this file's JSON payloads use
    // (matching the JSON convention of lowercase-word enum labels already used elsewhere in this
    // action layer, e.g. render.snapshot's format names, rather than goTypeToString()'s PascalCase --
    // that mirrors C++ enumerator spelling for internal/debug consumption; this one is meant to read
    // like ordinary API JSON, e.g. "individual"/"multisize").
    QString measurementsTypeToString(MeasurementsType type)
    {
        switch (type)
        {
            case MeasurementsType::Multisize:  return QStringLiteral("multisize");  // Gradation table, keyed by size/height.
            case MeasurementsType::Individual: return QStringLiteral("individual"); // Single fixed-value measurement set.
            case MeasurementsType::Unknown:    return QStringLiteral("unknown");    // Unrecognized file format (or qApp->patternType() never set yet).
        }
        return QStringLiteral("unknown"); // Never reached (enum has no other enumerators); satisfies -Wreturn-type without a compiler-specific pragma.
    }

    // Builds the same {"type","message",...extra} structured-error shape already established by
    // ActionEngine::run()'s nameResolution error, point_handlers.cpp's draftBlockExists error, and
    // formula_point_handlers.cpp's formulaError error, so every structured error in this file looks
    // like every other one an automated (AI) caller already knows how to parse.
    QJsonObject structuredError(const QString &type, const QString &message)
    {
        QJsonObject error; // Builds the shared {"type","message"} shape every structured error here uses.
        error["type"] = type;       // Stable, machine-readable category for this error family.
        error["message"] = message; // Human-readable summary.
        return error;                // Return the populated object; callers add op-specific fields before wrapping in ActionResult::failure().
    }

    // Result of the shared load logic below: distinct from ActionResult so handleMeasurementsSync()
    // can inspect measurementsLoaded/type on a successful load without re-parsing a JSON payload it
    // just built itself, and so a failed load's structured error can be reused verbatim as
    // handleMeasurementsLoad()'s own ActionResult::failure() value.
    struct LoadOutcome
    {
        bool ok = false;                              // True when every check passed and VContainer/doc were mutated; false otherwise (in which case nothing was mutated -- see loadMeasurementsFile()'s own comment on check-then-mutate ordering).
        QJsonValue error;                              // Populated (plain string or structured object) only when ok is false.
        int measurementsLoaded = 0;                    // data->DataMeasurements().size() immediately after a successful load; unused when ok is false.
        MeasurementsType type = MeasurementsType::Unknown; // The loaded file's own type; unused when ok is false.
    };

    // Shared logic behind both "measurements.load" and (as the first half of) "measurements.sync".
    // Mirrors MainWindow::openMeasurementFile() (validation/schema-upgrade) followed by
    // MainWindow::updateMeasurements() (mainwindow.cpp, ~lines 546-746 as of this writing --
    // re-locate by function name if the file has moved) line for line, without any MainWindow
    // dependency: every call below reaches MeasurementDoc/VContainer/VAbstractPattern directly, the
    // same objects ActionContext already exposes.
    LoadOutcome loadMeasurementsFile(const QJsonObject &args, const ActionContext &ctx)
    {
        LoadOutcome outcome; // Default-constructed: ok == false until every check below passes.

        const QString rawPath = args.value(QStringLiteral("path")).toString(); // The measurement file to load; required, no sane default.
        if (rawPath.isEmpty())
        {
            outcome.error = QStringLiteral("measurements.load requires a non-empty \"path\"");
            return outcome;
        }
        // Absolutized so the SetMPath() call below stores a path RelativeMPath()/AbsoluteMPath()
        // (vmisc/def.cpp) can correctly resolve later -- both only produce a correct result given an
        // absolute input; a relative one is returned unchanged instead of resolved against the
        // current directory (see RelativeMPath()'s own early-return), since the GUI's own equivalent
        // callers only ever pass paths a file-open dialog already made absolute.
        const QString path = QFileInfo(rawPath).absoluteFilePath();
        if (!QFileInfo::exists(path)) // Fail fast with a clear message, exactly as action_host.cpp's requireFileExists() does for the initial load.
        {
            outcome.error = QStringLiteral("measurements.load: file not found: %1").arg(path);
            return outcome;
        }

        VContainer *data = ctx.data();          // Local alias for the pattern's variable/data container.
        VAbstractPattern *doc = ctx.doc();      // Local alias for the pattern document; ListMeasurements() reads its formulas.
        if (data == nullptr || doc == nullptr) // Guard against a context missing either piece this handler reads/mutates.
        {
            outcome.error = QStringLiteral("measurements.load: context is missing a document or data container");
            return outcome;
        }

        // Mirrors MainWindow::openMeasurementFile()'s own construction order (mainwindow.cpp):
        // wire the live size/height pointers for gradation-aware formula evaluation before parsing,
        // the same order action_host.cpp's own initial measurement load already uses.
        MeasurementDoc measurements(data); // The new file's own MeasurementDoc, distinct from whatever measurement doc backs the pattern's already-loaded values.
        measurements.setSize(VContainer::rsize());   // Live pointer to VContainer's static _size; multisize gradation math reads through this.
        measurements.setHeight(VContainer::rheight()); // Live pointer to VContainer's static _height; multisize gradation math reads through this.

        try
        {
            measurements.setXMLContent(path); // Parses the raw XML and sets measurements.Type() via ReadType() (measurements.cpp).
        }
        catch (const VException &error) // A malformed/unreadable file throws here, exactly as VPatternConverter/MeasurementDoc do elsewhere in this daemon.
        {
            outcome.error = QStringLiteral("measurements.load: could not read %1: %2").arg(path, error.ErrorMessage());
            return outcome;
        }

        if (measurements.Type() == MeasurementsType::Unknown) // openMeasurementFile()'s own first check after setXMLContent().
        {
            outcome.error = QStringLiteral("measurements.load: %1 has an unrecognized measurement file format").arg(path);
            return outcome;
        }

        try
        {
            // Upgrades an older-format file to the schema readMeasurements() expects below -- the
            // same two-converter dance openMeasurementFile() performs (mainwindow.cpp), reused
            // verbatim rather than reimplemented, per this phase's explicit instruction.
            if (measurements.Type() == MeasurementsType::Multisize)
            {
                MultiSizeConverter converter(path);         // Upgrades a multisize (.vst/.smms) file in place.
                measurements.setXMLContent(converter.Convert()); // Re-parses the upgraded XML; Type() is re-derived but unchanged.
            }
            else
            {
                IndividualSizeConverter converter(path);     // Upgrades an individual (.vit/.smis) file in place.
                measurements.setXMLContent(converter.Convert()); // Re-parses the upgraded XML; Type() is re-derived but unchanged.
            }
        }
        catch (const VException &error) // A file whose declared version the converter doesn't recognize throws here.
        {
            outcome.error = QStringLiteral("measurements.load: could not upgrade %1 to the current schema: %2").arg(path, error.ErrorMessage());
            return outcome;
        }

        if (!measurements.eachKnownNameIsValid()) // openMeasurementFile()'s own guard against a corrupted/mistyped standard measurement name.
        {
            outcome.error = QStringLiteral("measurements.load: %1 contains an invalid known measurement name").arg(path);
            return outcome;
        }

        if (measurements.Type() == MeasurementsType::Multisize && measurements.measurementUnits() == Unit::Inch)
        {
            // openMeasurementFile()'s own domain constraint: gradation math assumes metric multisize
            // tables; matches its exact log message, minus the GUI-only qApp->exit() side effect.
            outcome.error = QStringLiteral("measurements.load: multisize measurement tables in inches are not supported");
            return outcome;
        }

        // checkRequiredMeasurements() equivalent (mainwindow.cpp): every measurement name any
        // formula in the pattern references must exist in the new file, or that formula would
        // silently evaluate against a now-missing variable once readMeasurements() below runs. Run
        // strictly before any mutation, per this phase's explicit requirement, so a failed check
        // leaves VContainer/doc byte-identical to before this action ran.
        const QStringList fileNames = measurements.ListAll();     // Every measurement name (known or custom) the new file defines.
        const QStringList patternNames = doc->ListMeasurements(); // Every measurement name any formula in the pattern currently references.
        const QSet<QString> fileNameSet(fileNames.begin(), fileNames.end()); // Set form for O(1) membership checks below.
        QStringList missing; // Accumulates pattern-required names absent from the new file, in doc->ListMeasurements()'s own order.
        for (const QString &name : patternNames)
        {
            if (!fileNameSet.contains(name))
            {
                missing.append(name); // This pattern-required name has no matching entry in the new file.
            }
        }
        if (!missing.isEmpty())
        {
            QJsonArray missingArray; // Converts the missing-name list into a JSON array for the structured error below.
            for (const QString &name : missing)
            {
                missingArray.append(name);
            }
            QJsonObject error = structuredError(QStringLiteral("missingMeasurements"),
                QStringLiteral("Measurement file doesn't include all the required measurements: %1").arg(missing.join(QStringLiteral(", "))));
            error["missing"] = missingArray; // Exact names, for a caller to react to programmatically instead of parsing the message string.
            outcome.error = QJsonValue(error);
            return outcome;
        }

        // Type-consistency guard, mirroring updateMeasurements()'s own check (mainwindow.cpp):
        // qApp->patternType() is set once -- by action_host.cpp's runActions(), right after its own
        // initial MeasurementDoc load, mirroring MainWindow::loadMeasurements()'s
        // qApp->setPatternType() call -- and this action's job is to verify a *later* file still
        // matches that established type, not to (re)establish it. Switching a pattern from
        // individual to multisize measurements (or back) via this action is unsupported, exactly as
        // the GUI's own Sync Measurements has no path to do so either.
        if (qApp->patternType() != measurements.Type())
        {
            QJsonObject error = structuredError(QStringLiteral("measurementTypeMismatch"),
                QStringLiteral("Measurement file type does not match the pattern's current measurement type"));
            error["expected"] = measurementsTypeToString(qApp->patternType()); // The type every prior load in this process established.
            error["actual"] = measurementsTypeToString(measurements.Type());   // The type this new file actually is.
            outcome.error = QJsonValue(error);
            return outcome;
        }

        // Every check above passed: now, and only now, mutate state. Mirrors updateMeasurements()'s
        // own ClearVariables()+readMeasurements() pair (mainwindow.cpp) exactly.
        try
        {
            data->ClearVariables(VarType::Measurement); // Drops every existing measurement variable; formulas re-resolve against the new file's values below.
            measurements.readMeasurements();             // Populates `data` with the new file's measurement variables (measurements.cpp).
        }
        catch (const VException &error) // readMeasurements() can throw VExceptionEmptyParameter reading a malformed <measurement>'s required "name" attribute (measurements.cpp); caught here exactly as updateMeasurements() itself catches it, so it reaches the caller as a JSON error instead of an uncaught exception.
        {
            outcome.error = QStringLiteral("measurements.load: error reading measurements from %1: %2").arg(path, error.ErrorMessage());
            return outcome;
        }

        // Records path back onto doc's own <measurements> element (VAbstractPattern::SetMPath()),
        // exactly as MainWindow::LoadIndividual()/LoadMultisize() do right after their own
        // successful load (mainwindow.cpp) -- without this, a pattern saved after a
        // measurements.load/measurements.sync action has no idea which measurement file it came
        // from, so every measurement-referencing formula fails to resolve when the saved file is
        // reopened, even though `data` (just repopulated above) looks entirely correct in-process.
        doc->SetMPath(RelativeMPath(qApp->getFilePath(), path));

        if (measurements.Type() == MeasurementsType::Multisize)
        {
            if (!args.contains(QStringLiteral("size")) || !args.contains(QStringLiteral("height")))
            {
                // updateMeasurements()'s "size"/"height" parameters are mandatory for its caller to
                // supply for a multisize file (mainwindow.cpp passes VContainer::size()/height()
                // when re-syncing); there is no sane default the way basePoint's mx/my have one.
                outcome.error = QStringLiteral("measurements.load requires \"size\" and \"height\" for a multisize measurement file");
                return outcome;
            }
            // updateMeasurements() applies size/height verbatim, with no unit conversion (unlike
            // loadMeasurements()'s *initial*-open path, which UnitConvertor()s the file's own
            // BaseSize()/BaseHeight()) -- its caller is expected to already have them in the
            // pattern's own unit space, exactly as syncMeasurements() re-passes VContainer::size()/
            // height() straight through. This JSON action's "size"/"height" are treated the same way.
            //
            // VContainer::setSize()/setHeight() write process-global *static* members (vcontainer.cpp),
            // not per-ActionContext/per-request state -- concurrent or interleaved requests against
            // different patterns in the same actiond process would clobber each other's active
            // size/height. actiond is one-shot-per-invocation as of Phase 2/6, so this is not
            // reachable yet; flagged here for whoever implements multi-session support later.
            VContainer::setSize(args.value(QStringLiteral("size")).toInt());     // "size" is a JSON number; toInt() reads it directly (no formula/string involved, unlike a tool's length field).
            VContainer::setHeight(args.value(QStringLiteral("height")).toInt()); // Same as above, for height.
        }
        else if (measurements.Type() == MeasurementsType::Individual)
        {
            // setSizeHeightForIndividualM() equivalent (mainwindowsnogui.cpp): an individual file's
            // own special size_M/height_M measurement variables (just populated into `data` by
            // readMeasurements() above) name the size/height this specific person's measurements
            // were taken at -- this JSON action's "size"/"height" args are ignored on this branch,
            // exactly as updateMeasurements() ignores its own size/height parameters here too.
            const QHash<QString, QSharedPointer<VInternalVariable>> *vars = data->DataVariables(); // Freshly repopulated by readMeasurements() above.
            VContainer::setSize(vars->contains(size_M) ? *vars->value(size_M)->GetValue() : 0.0);       // Falls back to 0, exactly as setSizeHeightForIndividualM() does when the file omits size_M.
            VContainer::setHeight(vars->contains(height_M) ? *vars->value(height_M)->GetValue() : 0.0); // Falls back to 0, exactly as setSizeHeightForIndividualM() does when the file omits height_M.
            // setSizeHeightForIndividualM() also calls doc->SetPatternWasChanged(true) and
            // `emit doc->updatePatternLabel()` here -- both are pure GUI-presentation bookkeeping (an
            // "unsaved changes" flag and a window-title-update signal with no slot connected in this
            // headless daemon), not state readMeasurements()/geometry recompute depends on, so they
            // are intentionally not replicated.
        }

        outcome.ok = true;                                        // Every check passed and state was mutated: this is a genuine success.
        outcome.measurementsLoaded = data->DataMeasurements().size(); // ClearVariables() just ran, so this count reflects only the file just loaded.
        outcome.type = measurements.Type();                        // Echoed back to the caller in the success payload.
        return outcome;
    }

    // Shared logic behind both "measurements.recompute" and (as the second half of)
    // "measurements.sync". Mirrors the doc->LiteParseTree(Document::LiteParse) call
    // MainWindow::syncMeasurements() itself makes (mainwindow.cpp) after a successful
    // updateMeasurements().
    ActionResult recomputePattern(const ActionContext &ctx)
    {
        VAbstractPattern *doc = ctx.doc(); // Local alias for the pattern document.
        if (doc == nullptr) // Guard against a context constructed without a document.
        {
            return ActionResult::failure(QStringLiteral("measurements.recompute: context is missing a document"));
        }

        try
        {
            // doc->LiteParseTree() (VAbstractPattern, pure virtual -- vabstractpattern.h;
            // VPattern::LiteParseTree(), vpattern.cpp, is the only override) is the exact call
            // syncMeasurements() makes, and the only recompute entry point reachable from an
            // actionlayer handler at all: VPattern::Parse() itself is declared directly on VPattern
            // (vpattern.h), not on VAbstractPattern, and actionlayer.pro deliberately does not
            // link/include vpattern.* (only actiond.pro/seamly2d.pro compile vpattern.cpp directly,
            // as a source file -- it belongs to the seamly2d app tree, not a shared lib; see
            // actiond.pro's SOURCES block). Going through the virtual base-class method is therefore
            // not just style here, it is the only option without restructuring the build.
            //
            // VERIFIED (by reading vpattern.cpp's LiteParseTree() body directly): it already catches
            // every VException subtype plus std::bad_alloc internally and, on failure, logs via
            // qCCritical and runs `if (not qApp->isAppInGUIMode()) { qApp->exit(V_EX_NOINPUT); }`.
            // ActiondApplication::isAppInGUIMode() (actiond_application.cpp) always returns false, so
            // that branch always runs -- but qApp->exit() only requests the *running event loop* to
            // stop, and actiond's main() (main.cpp) never calls QCoreApplication::exec(), so this
            // call is a silent no-op here: it neither terminates the process nor makes the failure
            // observable to this handler. Net effect: a VException-family failure during recompute is
            // swallowed by LiteParseTree() itself -- reported here as success, not as a crash, but
            // also not as a detected error; there is no return value or other externally-visible
            // signal LiteParseTree() offers to distinguish that case from a real success.
            //
            // The one exception type LiteParseTree() does NOT catch is qmu::QmuParserError: it is not
            // a VException subclass (qmuparsererror.h), and VAbstractTool::CheckFormula()
            // (vabstracttool.cpp) -- reached again here as the reparse recreates every tool -- rethrows
            // it uncaught in non-GUI mode via its own `else { throw; }`, exactly as Phase 6 already
            // established for the six formula-point tools. That one *does* propagate here uncaught,
            // so it is the one recompute failure this handler can, and does, catch and report below --
            // realistically also the dominant real-world failure mode for a measurement-triggered
            // recompute specifically (a formula newly dividing by a measurement that is now zero),
            // since the VException-family failures LiteParseTree() swallows are mostly about malformed
            // XML/missing tool ids, not something a measurement *value* change would newly trigger.
            doc->LiteParseTree(Document::LiteParse);
        }
        catch (const qmu::QmuParserError &error) // See the long comment above for why this is the one failure mode this handler can observe.
        {
            QJsonObject detail = structuredError(QStringLiteral("formulaError"),
                QStringLiteral("Formula error during recompute: %1").arg(error.GetMsg()));
            detail["message"] = error.GetMsg();  // qmu's own human-readable diagnostic.
            detail["expr"] = error.GetExpr();     // The specific sub-expression qmu was evaluating when it failed.
            return ActionResult::failure(QJsonValue(detail));
        }
        catch (const std::exception &error) // Catches anything else well-behaved that reaches here uncaught (see the comment above: VException itself should not, in practice).
        {
            return ActionResult::failure(QString::fromUtf8(error.what()));
        }
        catch (...) // Absolute last resort: guarantees no exception of any kind escapes this handler uncaught.
        {
            return ActionResult::failure(QStringLiteral("measurements.recompute: unknown error during recompute"));
        }

        QJsonObject payload; // Builds the documented {"success"} result.
        payload["success"] = true; // No further detail to report: LiteParseTree() either ran to completion or this handler already returned above.
        return ActionResult::success(payload);
    }
}

// Implements "measurements.load": see measurements_sync_handlers.h for the documented JSON shape.
ActionResult handleMeasurementsLoad(const QJsonObject &args, const ActionContext &ctx)
{
    const LoadOutcome outcome = loadMeasurementsFile(args, ctx); // Does all the real work; see its own comment for the full check/mutate sequence.
    if (!outcome.ok)
    {
        return ActionResult::failure(outcome.error); // Already a plain string or a structured {"type","message",...} object, as loadMeasurementsFile() built it.
    }

    QJsonObject payload; // Builds the documented {"success","measurementsLoaded","type"} result.
    payload["success"] = true;                                    // This op's own required minimum field, per this phase's error-handling contract.
    payload["measurementsLoaded"] = outcome.measurementsLoaded;    // How many measurement variables the new file populated.
    payload["type"] = measurementsTypeToString(outcome.type);      // "individual" or "multisize".
    return ActionResult::success(payload);
}

// Implements "measurements.recompute": see measurements_sync_handlers.h for the documented JSON shape.
ActionResult handleMeasurementsRecompute(const QJsonObject &args, const ActionContext &ctx)
{
    Q_UNUSED(args) // measurements.recompute takes no arguments; kept in the signature for uniformity with other handlers.
    return recomputePattern(ctx); // Does all the real work; see its own comment for what it can and cannot detect as a failure.
}

// Implements "measurements.sync": see measurements_sync_handlers.h for the documented JSON shape.
ActionResult handleMeasurementsSync(const QJsonObject &args, const ActionContext &ctx)
{
    const LoadOutcome outcome = loadMeasurementsFile(args, ctx); // First half: identical to handleMeasurementsLoad()'s own call.
    if (!outcome.ok)
    {
        return ActionResult::failure(outcome.error); // Load itself failed: nothing was mutated (see loadMeasurementsFile()'s own comment), so there is nothing to recompute.
    }

    const ActionResult recomputeResult = recomputePattern(ctx); // Second half: identical to handleMeasurementsRecompute()'s own call.
    if (!recomputeResult.ok)
    {
        // Unlike a measurements.load-only failure, load already mutated state by this point
        // (ClearVariables()+readMeasurements()+setSize()/setHeight() already ran successfully) --
        // surface that explicitly so a caller doesn't assume "sync failed" means "nothing changed".
        QJsonObject detail = recomputeResult.error.isObject() // Preserve recomputePattern()'s own structured error shape when it produced one...
            ? recomputeResult.error.toObject()
            : QJsonObject{{QStringLiteral("message"), recomputeResult.error.toString()}}; // ...or wrap its plain-string message the same way, if it produced that instead.
        detail["measurementsLoaded"] = outcome.measurementsLoaded; // How many measurement variables were loaded before the recompute step failed.
        detail["type"] = measurementsTypeToString(outcome.type);   // The type of the file that was loaded before the recompute step failed.
        return ActionResult::failure(QJsonValue(detail));
    }

    QJsonObject payload; // Builds the documented {"success","measurementsLoaded","type","recomputed"} result.
    payload["success"] = true;                                 // This op's own required minimum field, per this phase's error-handling contract.
    payload["measurementsLoaded"] = outcome.measurementsLoaded; // How many measurement variables the new file populated.
    payload["type"] = measurementsTypeToString(outcome.type);   // "individual" or "multisize".
    payload["recomputed"] = true;                                // Confirms the second (LiteParseTree) half also ran to completion.
    return ActionResult::success(payload);
}
