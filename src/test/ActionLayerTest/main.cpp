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

#include "tst_pattern_dump.h"          // Brings in TST_PatternDump, one of the test classes this binary runs.
#include "tst_render_snapshot.h"       // Brings in TST_RenderSnapshot, another test class this binary runs.
#include "tst_name_resolver.h"         // Brings in TST_NameResolver, Phase 4's pure-C++ NameResolver coverage.
#include "tst_action_engine_batches.h" // Brings in TST_ActionEngineBatches, Phase 4's JSON batch/fixture coverage.
#include "tst_action_schema.h"         // Brings in TST_ActionSchema, the `actiond --list-tools` drift-detection coverage.

#include "../../libs/vmisc/vabstractapplication.h" // Brings in VAbstractApplication; constructing any VAbstractPattern needs a live qApp of this type.
#include "../../libs/vpatterndb/vtranslatevars.h"  // Brings in VTranslateVars, the return type of translateVariables() below.
#include "../../libs/vmisc/vsettings.h"             // Brings in VSettings, constructed in openSettings() below.
#include "../../libs/vmisc/projectversion.h"        // Brings in VER_INTERNALNAME_2D_STR / VER_COMPANYNAME_STR for app identity.

#include <QtTest> // Provides QTest::qExec(), used to run the test class below.

// Minimal VAbstractApplication subclass so qApp->Settings() resolves during this binary's run.
// VAbstractPattern's own constructor reads default line settings via qApp, so any test that
// constructs a VAbstractPattern subclass needs exactly this kind of app instance alive first.
// Mirrors qttestmainlambda.cpp's TestApplication2D; duplicated here (rather than reused) because
// this is a separate standalone test binary, not a participant in the Seamly2DTests target.
class ActionLayerTestApplication : public VAbstractApplication
{
public:
    ActionLayerTestApplication(int &argc, char **argv); // Constructs the app and opens its settings.
    virtual ~ActionLayerTestApplication() Q_DECL_EQ_DEFAULT; // Default destruction is sufficient; nothing owned beyond base class.

    virtual const VTranslateVars *translateVariables(); // Required override; this test never formats translated formulas.
    virtual void                  openSettings();        // Required override; creates the VSettings qApp->Settings() returns.
    virtual bool                  isAppInGUIMode() const; // Required override; false keeps this binary non-interactive.
    virtual void                  initTranslateVariables(); // Required override; no-op, matching translateVariables() returning null.
};

// Constructs the base VApplication, names it, then opens settings so qApp->Settings() is valid.
ActionLayerTestApplication::ActionLayerTestApplication(int &argc, char **argv)
    : VAbstractApplication(argc, argv) // Base class does the actual QApplication setup.
{
    setApplicationName(VER_INTERNALNAME_2D_STR); // Matches the real app's identity for settings-file compatibility.
    setOrganizationName(VER_COMPANYNAME_STR);    // Matches the real app's identity for settings-file compatibility.
    openSettings();                              // Populate m_settings before any VAbstractPattern subclass is constructed.
}

// No translation-variable support is needed for a JSON-introspection test; returning null is safe
// because this test never calls a code path that formats a formula through it.
const VTranslateVars *ActionLayerTestApplication::translateVariables()
{
    return nullptr; // No translator needed; nothing under test calls into it.
}

// Creates the VSettings instance qApp->Settings() will return for the remainder of this run.
void ActionLayerTestApplication::openSettings()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, // Locate the standard per-user ini settings file...
                        QCoreApplication::organizationName(), QCoreApplication::applicationName()); // ...under this app's identity.
    m_settings = new VSettings(settings.fileName(), QSettings::IniFormat, this); // Wrap that same file as this app's VSettings.
}

// Running headless: no windows are created, so this always reports GUI mode as off.
bool ActionLayerTestApplication::isAppInGUIMode() const
{
    return false; // Headless test binary; nothing here presents UI.
}

// No translation variables to initialize for this test binary.
void ActionLayerTestApplication::initTranslateVariables()
{
    // Intentionally empty: this test never touches formula translation.
}

// Entry point: builds the one required app instance, then runs every test class under QTest.
int main(int argc, char **argv)
{
    ActionLayerTestApplication app(argc, argv); // Must exist before any VAbstractPattern subclass is constructed.

    int status = 0; // Accumulates a non-zero exit code if either test class reports any failure.

    TST_PatternDump patternDumpTest;                          // Exercises pattern.dump/listMeasurements/listTools.
    status |= QTest::qExec(&patternDumpTest, argc, argv);      // Runs every slot in this class; bitwise-OR preserves a prior failure's non-zero code.

    TST_RenderSnapshot renderSnapshotTest;                     // Exercises render.snapshot.
    status |= QTest::qExec(&renderSnapshotTest, argc, argv);   // Runs every slot in this class; bitwise-OR preserves a prior failure's non-zero code.

    TST_NameResolver nameResolverTest;                         // Exercises NameResolver directly (no JSON involved).
    status |= QTest::qExec(&nameResolverTest, argc, argv);     // Runs every slot in this class; bitwise-OR preserves a prior failure's non-zero code.

    TST_ActionEngineBatches actionEngineBatchesTest;            // Exercises pattern.resolveName and resolver-error serialization via full JSON batches.
    status |= QTest::qExec(&actionEngineBatchesTest, argc, argv); // Runs every slot in this class; bitwise-OR preserves a prior failure's non-zero code.

    TST_ActionSchema actionSchemaTest;                          // Exercises the `actiond --list-tools` command's drift-detection guarantees.
    status |= QTest::qExec(&actionSchemaTest, argc, argv);      // Runs every slot in this class; bitwise-OR preserves a prior failure's non-zero code.

    return status; // Non-zero if any test class reported any failure.
}
