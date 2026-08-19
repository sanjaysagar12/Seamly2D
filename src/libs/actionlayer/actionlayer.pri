# ADD TO EACH PATH $$PWD VARIABLE!!!!!!
# This need for corect working file translations.pro

SOURCES += \
    $$PWD/action_registry.cpp \
    $$PWD/action_engine.cpp \
    $$PWD/name_resolver.cpp \
    $$PWD/handlers/pattern_dump_handler.cpp \
    $$PWD/handlers/pattern_measurements_handler.cpp \
    $$PWD/handlers/pattern_list_tools_handler.cpp

*msvc*:SOURCES += $$PWD/stable.cpp

HEADERS += \
    $$PWD/action_context.h \
    $$PWD/action_registry.h \
    $$PWD/action_engine.h \
    $$PWD/action_result.h \
    $$PWD/action_error.h \
    $$PWD/name_resolver.h \
    $$PWD/stable.h \
    $$PWD/handlers/pattern_dump_handler.h \
    $$PWD/handlers/pattern_measurements_handler.h \
    $$PWD/handlers/pattern_list_tools_handler.h
