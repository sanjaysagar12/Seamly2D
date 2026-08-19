# ADD TO EACH PATH $$PWD VARIABLE!!!!!!
# This need for corect working file translations.pro

SOURCES += \
    $$PWD/action_registry.cpp \
    $$PWD/action_engine.cpp \
    $$PWD/name_resolver.cpp

*msvc*:SOURCES += $$PWD/stable.cpp

HEADERS += \
    $$PWD/action_context.h \
    $$PWD/action_registry.h \
    $$PWD/action_engine.h \
    $$PWD/name_resolver.h \
    $$PWD/stable.h
