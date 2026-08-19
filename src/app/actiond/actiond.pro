#-------------------------------------------------
#
# Project created by QtCreator 2026-08-19T00:00:00
#
#-------------------------------------------------
message("Entering actiond.pro")

# File with common stuff for whole project
include(../../../common.pri)

# core: QJsonDocument/QCommandLineParser/etc. widgets+gui: VMainGraphicsScene (QGraphicsScene) and
# every VAbstractPattern subclass ultimately needs a QApplication (VAbstractApplication : QApplication).
# xml: QDomDocument, used throughout ifc's VDomDocument-derived classes actiond compiles against.
# printsupport: required transitively -- src/libs/vmisc/def.h includes <QPrinter>, and def.h is
# included practically everywhere in this dependency chain (confirmed empirically in Phase 1/2:
# omitting it fails with "Cannot open include file: 'QPrinter'"). multimedia: required at link time --
# vtools.lib's PatternPieceDialog uses QSoundEffect for a UI sound cue; actiond never shows that
# dialog, but the static lib still needs the symbol resolved. network and svg are NOT included:
# nothing actiond links against needs sockets, and svg icon rendering is GUI-only.
QT += core gui widgets xml printsupport multimedia

# Name of binary file
TARGET = actiond

# We want a console executable, not a library.
TEMPLATE = app

# actiond is a command-line tool: force the console subsystem on Windows so stdout/stderr attach
# to the invoking console (a plain TEMPLATE=app target otherwise defaults to the GUI subsystem
# there, which would silently swallow every stdout/stderr write this tool depends on).
CONFIG += console

# No .app bundle: a bundled binary would run from inside a wrapper directory, which is wrong for
# a CLI tool meant to be invoked directly and piped like any other console program.
CONFIG -= app_bundle

# Directory for executable file
DESTDIR = bin

# Directory for files created moc
MOC_DIR = moc

# Directory for objects files
OBJECTS_DIR = obj

# precompiled headers clash with the BUILD_REVISION define, thus disable here (same reason
# seamly2d.pro/seamlyme.pro both disable it -- see FindBuildRevision() below).
CONFIG -= precompile_header

DVCS_HESH=$$FindBuildRevision()
message("actiond.pro: Build revision:" $${DVCS_HESH})
DEFINES += "BUILD_REVISION=$${DVCS_HESH}" # Make available build revision number in sources, matching seamly2d/seamlyme.

SOURCES += \
    main.cpp \
    actiond_application.cpp \
    action_host.cpp \
    ../seamly2d/xml/vpattern.cpp

HEADERS += \
    actiond_application.h \
    action_host.h \
    ../seamly2d/xml/vpattern.h

include(warnings.pri)

# When the GNU linker sees a library, it discards all symbols that it doesn't need.
# Dependent library go first.

#Tools static library (depend on VWidgets, VMisc, VPatternDB) -- provides ImageItem/ImageTool,
# which VPattern::parseImageElement() (background-image support) links against.
unix|win32: LIBS += -L$$OUT_PWD/../../libs/tools/$${DESTDIR}/ -ltools

INCLUDEPATH += $$PWD/../../libs/tools
DEPENDPATH += $$PWD/../../libs/tools

win32:!win32-g++: PRE_TARGETDEPS += $$OUT_PWD/../../libs/tools/$${DESTDIR}/tools.lib
else:unix|win32-g++: PRE_TARGETDEPS += $$OUT_PWD/../../libs/tools/$${DESTDIR}/libtools.a

#VTools static library (depend on VWidgets, VMisc, VPatternDB) -- required because VPattern::Parse()
# calls into vtools' per-tool ::Create() factories (VToolBasePoint::Create(), VToolEndLine::Create(),
# etc.) to build the real tool history and graphics items; this is not optional for real parsing.
unix|win32: LIBS += -L$$OUT_PWD/../../libs/vtools/$${DESTDIR}/ -lvtools

INCLUDEPATH += $$PWD/../../libs/vtools
INCLUDEPATH += $$OUT_PWD/../../libs/vtools/$${UI_DIR} # For UI files
DEPENDPATH += $$PWD/../../libs/vtools

win32:!win32-g++: PRE_TARGETDEPS += $$OUT_PWD/../../libs/vtools/$${DESTDIR}/vtools.lib
else:unix|win32-g++: PRE_TARGETDEPS += $$OUT_PWD/../../libs/vtools/$${DESTDIR}/libvtools.a

#VWidgets static library (depend on: VMainGraphicsScene, VAbstractMainWindow -- both used directly)
unix|win32: LIBS += -L$$OUT_PWD/../../libs/vwidgets/$${DESTDIR}/ -lvwidgets

INCLUDEPATH += $$PWD/../../libs/vwidgets
DEPENDPATH += $$PWD/../../libs/vwidgets

win32:!win32-g++: PRE_TARGETDEPS += $$OUT_PWD/../../libs/vwidgets/$${DESTDIR}/vwidgets.lib
else:unix|win32-g++: PRE_TARGETDEPS += $$OUT_PWD/../../libs/vwidgets/$${DESTDIR}/libvwidgets.a

# VFormat static library (depend on VPatternDB, IFC) -- provides MeasurementDoc.
unix|win32: LIBS += -L$$OUT_PWD/../../libs/vformat/$${DESTDIR}/ -lvformat

INCLUDEPATH += $$PWD/../../libs/vformat
DEPENDPATH += $$PWD/../../libs/vformat

win32:!win32-g++: PRE_TARGETDEPS += $$OUT_PWD/../../libs/vformat/$${DESTDIR}/vformat.lib
else:unix|win32-g++: PRE_TARGETDEPS += $$OUT_PWD/../../libs/vformat/$${DESTDIR}/libvformat.a

#VPatternDB static library (depend on vgeometry, vmisc, VLayout) -- provides VContainer.
unix|win32: LIBS += -L$$OUT_PWD/../../libs/vpatterndb/$${DESTDIR} -lvpatterndb

INCLUDEPATH += $$PWD/../../libs/vpatterndb
DEPENDPATH += $$PWD/../../libs/vpatterndb

win32:!win32-g++: PRE_TARGETDEPS += $$OUT_PWD/../../libs/vpatterndb/$${DESTDIR}/vpatterndb.lib
else:unix|win32-g++: PRE_TARGETDEPS += $$OUT_PWD/../../libs/vpatterndb/$${DESTDIR}/libvpatterndb.a

# IFC static library (depend on QMuParser, VMisc) -- provides VAbstractPattern, VPatternConverter.
unix|win32: LIBS += -L$$OUT_PWD/../../libs/ifc/$${DESTDIR}/ -lifc

INCLUDEPATH += $$PWD/../../libs/ifc
DEPENDPATH += $$PWD/../../libs/ifc

win32:!win32-g++: PRE_TARGETDEPS += $$OUT_PWD/../../libs/ifc/$${DESTDIR}/ifc.lib
else:unix|win32-g++: PRE_TARGETDEPS += $$OUT_PWD/../../libs/ifc/$${DESTDIR}/libifc.a

#VMisc static library -- provides VAbstractApplication, VSettings.
unix|win32: LIBS += -L$$OUT_PWD/../../libs/vmisc/$${DESTDIR}/ -lvmisc

INCLUDEPATH += $$PWD/../../libs/vmisc
DEPENDPATH += $$PWD/../../libs/vmisc

win32:!win32-g++: PRE_TARGETDEPS += $$OUT_PWD/../../libs/vmisc/$${DESTDIR}/vmisc.lib
else:unix|win32-g++: PRE_TARGETDEPS += $$OUT_PWD/../../libs/vmisc/$${DESTDIR}/libvmisc.a

# VGeometry static library (depend on ifc)
unix|win32: LIBS += -L$$OUT_PWD/../../libs/vgeometry/$${DESTDIR}/ -lvgeometry

INCLUDEPATH += $$PWD/../../libs/vgeometry
DEPENDPATH += $$PWD/../../libs/vgeometry

win32:!win32-g++: PRE_TARGETDEPS += $$OUT_PWD/../../libs/vgeometry/$${DESTDIR}/vgeometry.lib
else:unix|win32-g++: PRE_TARGETDEPS += $$OUT_PWD/../../libs/vgeometry/$${DESTDIR}/libvgeometry.a

# VLayout static library
unix|win32: LIBS += -L$$OUT_PWD/../../libs/vlayout/$${DESTDIR}/ -lvlayout

INCLUDEPATH += $$PWD/../../libs/vlayout
DEPENDPATH += $$PWD/../../libs/vlayout

win32:!win32-g++: PRE_TARGETDEPS += $$OUT_PWD/../../libs/vlayout/$${DESTDIR}/vlayout.lib
else:unix|win32-g++: PRE_TARGETDEPS += $$OUT_PWD/../../libs/vlayout/$${DESTDIR}/libvlayout.a

# QMuParser library
unix|win32: LIBS += -L$${OUT_PWD}/../../libs/qmuparser/$${DESTDIR} -lqmuparser

INCLUDEPATH += $${PWD}/../../libs/qmuparser
DEPENDPATH += $${PWD}/../../libs/qmuparser

win32:!win32-g++: PRE_TARGETDEPS += $$OUT_PWD/../../libs/qmuparser/$${DESTDIR}/qmuparser.lib
else:unix|win32-g++: PRE_TARGETDEPS += $$OUT_PWD/../../libs/qmuparser/$${DESTDIR}/libqmuparser.a

# VPropertyExplorer library (depend on: vtools' property-browser dialogs)
unix|win32: LIBS += -L$${OUT_PWD}/../../libs/vpropertyexplorer/$${DESTDIR} -lvpropertyexplorer

INCLUDEPATH += $${PWD}/../../libs/vpropertyexplorer
DEPENDPATH += $${PWD}/../../libs/vpropertyexplorer

win32:!win32-g++: PRE_TARGETDEPS += $$OUT_PWD/../../libs/vpropertyexplorer/$${DESTDIR}/vpropertyexplorer.lib
else:unix|win32-g++: PRE_TARGETDEPS += $$OUT_PWD/../../libs/vpropertyexplorer/$${DESTDIR}/libvpropertyexplorer.a

#ActionLayer static library (depends on vpatterndb, vgeometry, ifc, vmisc)
unix|win32: LIBS += -L$$OUT_PWD/../../libs/actionlayer/$${DESTDIR}/ -lactionlayer

INCLUDEPATH += $$PWD/../../libs/actionlayer
DEPENDPATH += $$PWD/../../libs/actionlayer

win32:!win32-g++: PRE_TARGETDEPS += $$OUT_PWD/../../libs/actionlayer/$${DESTDIR}/actionlayer.lib
else:unix|win32-g++: PRE_TARGETDEPS += $$OUT_PWD/../../libs/actionlayer/$${DESTDIR}/libactionlayer.a

# xerces library -- VPatternConverter/VDomDocument's XML schema validation needs it.
macx: LIBS += -L$${PWD}/../../libs/xerces-c/macx/lib -lxerces-c
else:unix: LIBS += -lxerces-c
win32-msvc: LIBS += -L$${PWD}/../../libs/xerces-c/msvc/lib -lxerces-c_3
win32-arm64-msvc: LIBS += -L$${PWD}/../../libs/xerces-c/msvc-arm64/lib -lxerces-c_3
win32-g++: LIBS += -L$${PWD}/../../libs/xerces-c/mingw/lib -lxerces-c

win32 {
    copyToDestdir($${PWD}/$$INSTALL_XERCES, $$shell_path($${OUT_PWD}/$$DESTDIR))
}

# run windeployqt to include all qt libraries in $${DESTDIR}, exactly as seamly2d.pro/seamlyme.pro
# do -- actiond needs the same Qt runtime DLLs present alongside it to run standalone.
win32-msvc{
    # Trailing escape_expand ends this as its own command line -- without it, the next
    # QMAKE_POST_LINK += below (the qoffscreen.dll copy) gets glued onto this one as a bogus
    # extra argument to windeployqt instead of running as a separate step.
    QMAKE_POST_LINK += windeployqt $$shell_path($$DESTDIR/$${TARGET}.exe) $$escape_expand(\\n\\t)
}
win32-arm64-msvc{
    qtPrepareTool(WINDEPLOYQT, windeployqt)
    QMAKE_POST_LINK += $$WINDEPLOYQT --qtpaths $$shell_path($$[QT_INSTALL_BINS]/host-qtpaths.bat) $$shell_path($$DESTDIR/$${TARGET}.exe) $$escape_expand(\\n\\t)
}

# windeployqt only deploys qwindows.dll by default (it infers platform needs from the exe, and
# actiond looks like an ordinary Windows GUI-capable app to that inference). actiond's whole point
# is running with QT_QPA_PLATFORM=offscreen with no Qt SDK installed alongside it (a CI/container),
# so the offscreen platform plugin has to be deployed explicitly too, not left to an external
# QT_PLUGIN_PATH the caller may not have set.
win32{
    copyToDestdir($$shell_path($$[QT_INSTALL_PLUGINS]/platforms/qoffscreen.dll), $$shell_path($${OUT_PWD}/$$DESTDIR/platforms))
}
