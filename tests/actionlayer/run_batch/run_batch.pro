#-------------------------------------------------
#
# Project created for the actionlayer test harness rebuild, 20 Aug 2026.
#
#-------------------------------------------------
# run_batch is a small, dependency-free (Qt Core only -- no Seamly2D libs) console tool that
# drives the already-built seamly2d-actiond binary (src/app/actiond) as a subprocess -- one
# actions.json per invocation -- and diffs its JSON response against a golden file, exactly the
# way tests/actionlayer/run_batch.py (the harness this replaces) already did. Shelling out to the
# real actiond.exe, rather than re-linking ActionEngine/ActionHost directly into this binary, means
# this tool always tests the exact artifact that ships, and avoids duplicating actiond.pro's long,
# fragile static-lib link recipe (windeployqt, xerces, offscreen platform plugin, ...) a second time.

message("Entering run_batch.pro")

include(../../../common.pri)

QT       += core
QT       -= gui

TARGET = run_batch

CONFIG += console
CONFIG -= app_bundle

# common.pri turns on precompiled headers (stable.h/stable.cpp) in release mode; this directory
# has neither, matching actiond.pro's own reason for opting out (BUILD_REVISION/PCH clash) --
# run_batch is a small single-TU tool with nothing worth precompiling anyway.
CONFIG -= precompile_header

# CONFIG += testcase adds a 'make check' target that runs this binary with NO arguments -- see
# main()'s "no positional args" branch below, which runs every tests/actionlayer/scripts/*.json
# case against the default fixtures and exits non-zero if any fails. no_testcase_installs drops
# the 'make install' step testcase would otherwise add, matching every other test target here.
CONFIG += testcase no_testcase_installs

DESTDIR = bin
MOC_DIR = moc
OBJECTS_DIR = obj

SOURCES += main.cpp

# Absolute path to tests/actionlayer/ (this .pro file's own parent directory), baked in at compile
# time so the built binary can always find fixtures/scripts/expected/output regardless of where
# qmake happens to place the build output tree. This tool is a test-suite runner, not a relocatable
# installed product, so a compile-time absolute path is the right tradeoff (same idea as QFINDTESTDATA
# elsewhere in this repo's test suites, minus the QtTest dependency).
DEFINES += ACTIONLAYER_TESTS_DIR=\\\"$$PWD/..\\\"
