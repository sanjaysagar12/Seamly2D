//---------------------------------------------------------------------------------------------------------------------
//  @file   actiond_application.h
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

#ifndef ACTIOND_APPLICATION_H // Include guard start, prevents this header being processed twice in one translation unit.
#define ACTIOND_APPLICATION_H // Marks ACTIOND_APPLICATION_H as defined for the remainder of the include guard.

#include "../../libs/vmisc/vabstractapplication.h" // Brings in VAbstractApplication, the base class this app implements.
#include "../../libs/vpatterndb/vtranslatevars.h"   // Brings in VTranslateVars, owned below and returned by translateVariables().

// ActiondApplication is the minimal VAbstractApplication concrete subclass actiond needs to
// exist as the process's qApp singleton. Every VAbstractPattern (VPattern included) reads
// default settings via qApp during construction, and VPattern::Parse() reaches for several
// VAbstractApplication accessors (patternUnit, Settings, current document/data, etc.), so this
// object must be alive -- and be the QCoreApplication::instance() -- before any pattern loading
// happens. Deliberately does not derive from Application2D (seamly2d's own concrete subclass):
// that class pulls in command-line export, autosave, and theming machinery actiond has no use
// for, and redefines the qApp macro to its own type, which would fight this class's role.
class ActiondApplication : public VAbstractApplication
{
public:
    ActiondApplication(int &argc, char **argv); // Constructs the app and opens its settings; implemented in the .cpp.
    virtual ~ActiondApplication() Q_DECL_EQ_DEFAULT; // Default destruction; nothing owned beyond base class and m_translateVars below.

    virtual const VTranslateVars *translateVariables() override; // Required override; returns a real instance (see m_translateVars).
    virtual void                  openSettings() override;        // Required override; creates the VSettings qApp->Settings() returns.
    virtual bool                  isAppInGUIMode() const override; // Required override; always false, matching actiond's headless nature.
    virtual void                  initTranslateVariables() override; // Required override; no extra setup needed beyond the constructor.

private:
    // Real (not null) instance: unlike Phase 1's test double, actiond runs genuine formula
    // evaluation over real pattern files, and VTranslateVars is what lets formula tokens survive
    // localized round-tripping during that evaluation.
    VTranslateVars m_translateVars; // Owned for the lifetime of this application object.
};

#endif // ACTIOND_APPLICATION_H // End of include guard started above.
