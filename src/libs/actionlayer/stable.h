//---------------------------------------------------------------------------------------------------------------------
//  @file   stable.h
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

#ifndef STABLE_H // Include guard start, prevents this header being processed twice in one translation unit.
#define STABLE_H // Marks STABLE_H as defined for the remainder of the include guard.

/* Add C includes here */ // Placeholder matching the project's standard precompiled-header layout.

#if defined __cplusplus // Only pull in C++-only Qt umbrella headers when compiling as C++.
/* Add C++ includes here */ // Placeholder matching the project's standard precompiled-header layout.

#ifdef QT_CORE_LIB // Only include QtCore's umbrella header if the core module is actually linked.
#include <QtCore> // Pulls in all QtCore headers for the precompiled header, per project convention.
#endif

#ifdef QT_GUI_LIB // Only include QtGui's umbrella header if the gui module is actually linked.
#   include <QtGui> // Pulls in all QtGui headers for the precompiled header, per project convention.
#endif

#endif/*__cplusplus*/ // End of the C++-only block started above.

#endif // STABLE_H // End of include guard started above.
