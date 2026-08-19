//---------------------------------------------------------------------------------------------------------------------
//  @file   actiond_application.cpp
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

#include "actiond_application.h" // Brings in the ActiondApplication class declaration this file implements.

#include "../../libs/vmisc/vsettings.h"      // Brings in VSettings, constructed in openSettings() below.
#include "../../libs/vmisc/projectversion.h" // Brings in VER_INTERNALNAME_2D_STR / VER_COMPANYNAME_STR for app identity.

#include <QSettings> // Provides QSettings, used to locate the per-user ini file path below.

// Constructs the base VApplication, names it, then opens settings so qApp->Settings() is valid
// before any caller (main.cpp) proceeds to construct a VAbstractPattern.
ActiondApplication::ActiondApplication(int &argc, char **argv)
    : VAbstractApplication(argc, argv) // Base class performs the actual QApplication setup.
{
    setApplicationName(VER_INTERNALNAME_2D_STR); // Matches seamly2d's own identity, so settings files are compatible/shared.
    setOrganizationName(VER_COMPANYNAME_STR);    // Matches seamly2d's own identity, so settings files are compatible/shared.
    openSettings();                              // Populate m_settings before any VAbstractPattern subclass is constructed.
}

// Returns the real VTranslateVars instance formula evaluation needs during pattern parsing.
const VTranslateVars *ActiondApplication::translateVariables()
{
    return &m_translateVars; // Address of the member owned for this application's lifetime.
}

// Creates the VSettings instance qApp->Settings() will return for the remainder of this run.
void ActiondApplication::openSettings()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, // Locate the standard per-user ini settings file...
                        QCoreApplication::organizationName(), QCoreApplication::applicationName()); // ...under this app's identity.
    m_settings = new VSettings(settings.fileName(), QSettings::IniFormat, this); // Wrap that same file as this app's VSettings.
}

// actiond never shows a window: always report GUI mode as off so error paths in reused Seamly2D
// code (e.g. VPattern::Parse()'s catch blocks) skip any UI-only behavior.
bool ActiondApplication::isAppInGUIMode() const
{
    return false; // Headless by design; nothing here presents UI.
}

// No extra translation-variable setup needed beyond constructing m_translateVars itself.
void ActiondApplication::initTranslateVariables()
{
    // Intentionally empty: m_translateVars is fully initialized by its own default constructor.
}
