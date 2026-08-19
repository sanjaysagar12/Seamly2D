#-------------------------------------------------
#
# Project created by QtCreator 2026-08-19T00:00:00
#
#-------------------------------------------------

# Standalone test target for the actionlayer library. Kept as its own SUBDIRS project
# (mirroring ParserTest/CollectionTest/TranslationsTest) rather than folded into the
# monolithic Seamly2DTests binary, since that binary's registration point
# (qttestmainlambda.cpp) is outside actionlayer's phase-1 change scope.

QT       += core testlib gui widgets printsupport xml

TARGET = ActionLayerTest

# File with common stuff for whole project
include(../../../common.pri)

# CONFIG += testcase adds a 'make check' which is great. But by default it also
# adds a 'make install' that installs the test cases, which we do not want.
# Can configure it not to do that with 'no_testcase_installs'
CONFIG += testcase no_testcase_installs

# This binary constructs a real QApplication (VAbstractApplication is one), so a plain
# TEMPLATE=app target defaults to the GUI subsystem on Windows -- which silently swallows
# every QTest stdout/stderr write, even under shell redirection (exit code is still correct,
# but no PASS/FAIL text is ever visible). Forces the console subsystem instead, exactly as
# actiond.pro already does for the same reason.
CONFIG += console

# directory for executable file
DESTDIR = bin

# Directory for files created moc
MOC_DIR = moc

# objecs files
OBJECTS_DIR = obj

SOURCES += \
    main.cpp \
    tst_pattern_dump.cpp \
    tst_render_snapshot.cpp \
    tst_name_resolver.cpp \
    tst_action_engine_batches.cpp

*msvc*:SOURCES += stable.cpp

HEADERS += \
    stable.h \
    test_pattern_doc.h \
    tst_pattern_dump.h \
    tst_render_snapshot.h \
    tst_name_resolver.h \
    tst_action_engine_batches.h

# Fixture JSON files for tst_action_engine_batches.cpp; not compiled, but listed so they show up
# in IDEs and so `make dist`/packaging steps that walk DISTFILES pick them up. Loaded at test-run
# time via QFINDTESTDATA, not through Qt's resource system.
DISTFILES += \
    fixtures/action_batches/01_resolve_existing.json \
    fixtures/action_batches/02_resolve_missing.json \
    fixtures/action_batches/03_multi_action_chain.json \
    fixtures/action_batches/04_duplicate_name_guard.json \
    fixtures/expected/01_resolve_existing.expected.json \
    fixtures/expected/02_resolve_missing.expected.json \
    fixtures/expected/03_multi_action_chain.expected.json \
    fixtures/expected/04_duplicate_name_guard.expected.json

include(warnings.pri)

#VTools static library (depend on VWidgets, VMisc, VPatternDB)
unix|win32: LIBS += -L$$OUT_PWD/../../libs/vtools/$${DESTDIR}/ -lvtools

INCLUDEPATH += $$PWD/../../libs/vtools
DEPENDPATH += $$PWD/../../libs/vtools

win32:!win32-g++: PRE_TARGETDEPS += $$OUT_PWD/../../libs/vtools/$${DESTDIR}/vtools.lib
else:unix|win32-g++: PRE_TARGETDEPS += $$OUT_PWD/../../libs/vtools/$${DESTDIR}/libvtools.a

#VWidgets static library
unix|win32: LIBS += -L$$OUT_PWD/../../libs/vwidgets/$${DESTDIR}/ -lvwidgets

INCLUDEPATH += $$PWD/../../libs/vwidgets
DEPENDPATH += $$PWD/../../libs/vwidgets

win32:!win32-g++: PRE_TARGETDEPS += $$OUT_PWD/../../libs/vwidgets/$${DESTDIR}/vwidgets.lib
else:unix|win32-g++: PRE_TARGETDEPS += $$OUT_PWD/../../libs/vwidgets/$${DESTDIR}/libvwidgets.a

#VPatternDB static library (depend on vgeometry, vmisc, VLayout)
unix|win32: LIBS += -L$$OUT_PWD/../../libs/vpatterndb/$${DESTDIR} -lvpatterndb

INCLUDEPATH += $$PWD/../../libs/vpatterndb
DEPENDPATH += $$PWD/../../libs/vpatterndb

win32:!win32-g++: PRE_TARGETDEPS += $$OUT_PWD/../../libs/vpatterndb/$${DESTDIR}/vpatterndb.lib
else:unix|win32-g++: PRE_TARGETDEPS += $$OUT_PWD/../../libs/vpatterndb/$${DESTDIR}/libvpatterndb.a

# IFC static library (depend on QMuParser, VMisc)
unix|win32: LIBS += -L$$OUT_PWD/../../libs/ifc/$${DESTDIR}/ -lifc

INCLUDEPATH += $$PWD/../../libs/ifc
DEPENDPATH += $$PWD/../../libs/ifc

win32:!win32-g++: PRE_TARGETDEPS += $$OUT_PWD/../../libs/ifc/$${DESTDIR}/ifc.lib
else:unix|win32-g++: PRE_TARGETDEPS += $$OUT_PWD/../../libs/ifc/$${DESTDIR}/libifc.a

# VGeometry static library (depend on ifc)
unix|win32: LIBS += -L$$OUT_PWD/../../libs/vgeometry/$${DESTDIR} -lvgeometry

INCLUDEPATH += $$PWD/../../libs/vgeometry
DEPENDPATH += $$PWD/../../libs/vgeometry

win32:!win32-g++: PRE_TARGETDEPS += $$OUT_PWD/../../libs/vgeometry/$${DESTDIR}/vgeometry.lib
else:unix|win32-g++: PRE_TARGETDEPS += $$OUT_PWD/../../libs/vgeometry/$${DESTDIR}/libvgeometry.a

#VMisc static library
unix|win32: LIBS += -L$$OUT_PWD/../../libs/vmisc/$${DESTDIR}/ -lvmisc

INCLUDEPATH += $$PWD/../../libs/vmisc
DEPENDPATH += $$PWD/../../libs/vmisc

win32:!win32-g++: PRE_TARGETDEPS += $$OUT_PWD/../../libs/vmisc/$${DESTDIR}/vmisc.lib
else:unix|win32-g++: PRE_TARGETDEPS += $$OUT_PWD/../../libs/vmisc/$${DESTDIR}/libvmisc.a

# VLayout static library
unix|win32: LIBS += -L$$OUT_PWD/../../libs/vlayout/$${DESTDIR} -lvlayout

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

#ActionLayer static library (depends on vpatterndb, vgeometry, ifc, vmisc)
unix|win32: LIBS += -L$$OUT_PWD/../../libs/actionlayer/$${DESTDIR}/ -lactionlayer

INCLUDEPATH += $$PWD/../../libs/actionlayer
DEPENDPATH += $$PWD/../../libs/actionlayer

win32:!win32-g++: PRE_TARGETDEPS += $$OUT_PWD/../../libs/actionlayer/$${DESTDIR}/actionlayer.lib
else:unix|win32-g++: PRE_TARGETDEPS += $$OUT_PWD/../../libs/actionlayer/$${DESTDIR}/libactionlayer.a

# xerces library
macx: LIBS += -L$${PWD}/../../libs/xerces-c/macx/lib -lxerces-c
else:unix: LIBS += -lxerces-c
win32-msvc: LIBS += -L$${PWD}/../../libs/xerces-c/msvc/lib -lxerces-c_3
win32-arm64-msvc: LIBS += -L$${PWD}/../../libs/xerces-c/msvc-arm64/lib -lxerces-c_3
win32-g++: LIBS += -L$${PWD}/../../libs/xerces-c/mingw/lib -lxerces-c
