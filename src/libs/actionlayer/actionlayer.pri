# ADD TO EACH PATH $$PWD VARIABLE!!!!!!
# This need for corect working file translations.pro

SOURCES += \
    $$PWD/action_registry.cpp \
    $$PWD/action_engine.cpp \
    $$PWD/name_resolver.cpp \
    $$PWD/tool_catalog.cpp \
    $$PWD/handlers/pattern_dump_handler.cpp \
    $$PWD/handlers/pattern_measurements_handler.cpp \
    $$PWD/handlers/pattern_list_tools_handler.cpp \
    $$PWD/handlers/render_handlers.cpp \
    $$PWD/handlers/export_handlers.cpp \
    $$PWD/handlers/scene_render_geometry.cpp \
    $$PWD/handlers/pattern_resolve_name_handler.cpp \
    $$PWD/handlers/point_handlers.cpp \
    $$PWD/handlers/line_handlers.cpp \
    $$PWD/handlers/formula_point_handlers.cpp \
    $$PWD/handlers/measurements_sync_handlers.cpp \
    $$PWD/handlers/curve_handlers.cpp \
    $$PWD/handlers/cutpoint_handlers.cpp \
    $$PWD/handlers/operation_handlers.cpp \
    $$PWD/handlers/piece_handlers.cpp \
    $$PWD/handlers/point_edit_handlers.cpp \
    $$PWD/handlers/session_handlers.cpp \
    $$PWD/handlers/history_undo_handlers.cpp

*msvc*:SOURCES += $$PWD/stable.cpp

HEADERS += \
    $$PWD/action_context.h \
    $$PWD/action_registry.h \
    $$PWD/action_engine.h \
    $$PWD/action_result.h \
    $$PWD/action_schema.h \
    $$PWD/action_error.h \
    $$PWD/name_resolver.h \
    $$PWD/tool_catalog.h \
    $$PWD/stable.h \
    $$PWD/handlers/pattern_dump_handler.h \
    $$PWD/handlers/pattern_measurements_handler.h \
    $$PWD/handlers/pattern_list_tools_handler.h \
    $$PWD/handlers/render_handlers.h \
    $$PWD/handlers/export_handlers.h \
    $$PWD/handlers/scene_render_geometry.h \
    $$PWD/handlers/pattern_resolve_name_handler.h \
    $$PWD/handlers/point_handlers.h \
    $$PWD/handlers/line_handlers.h \
    $$PWD/handlers/formula_point_handlers.h \
    $$PWD/handlers/measurements_sync_handlers.h \
    $$PWD/handlers/curve_handlers.h \
    $$PWD/handlers/cutpoint_handlers.h \
    $$PWD/handlers/operation_handlers.h \
    $$PWD/handlers/piece_handlers.h \
    $$PWD/handlers/point_edit_handlers.h \
    $$PWD/handlers/session_handlers.h \
    $$PWD/handlers/history_undo_handlers.h
