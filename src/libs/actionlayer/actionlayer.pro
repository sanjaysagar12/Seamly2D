#-------------------------------------------------
#
# Project created by QtCreator 2026-08-19T00:00:00
#
#-------------------------------------------------

# File with common stuff for whole project
message("Entering actionlayer.pro")
include(../../../common.pri)

QT += core widgets printsupport xml

# Name of the library
TARGET = actionlayer

# We want create a library
TEMPLATE = lib

CONFIG += staticlib

include(actionlayer.pri)

# This is static library so no need in "make install"

# directory for executable file
DESTDIR = bin

# files created moc
MOC_DIR = moc

# objecs files
OBJECTS_DIR = obj

include(warnings.pri)

include (../libs.pri)
