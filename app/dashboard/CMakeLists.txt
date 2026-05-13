# Dashboard APP sources

set(gui_target_name honeygui_${c_CURRENT_IMAGE})

get_target_property(honeygui_includes ${gui_target_name} INTERFACE_INCLUDE_DIRECTORIES)

get_filename_component(HONEYGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../.. ABSOLUTE)

set(private_sources)
ameba_list_append(private_sources
    ui_design/dashboardEntry.c
    ui_design/callbacks/DashboardMain_callbacks.c
    ui_design/ui/DashboardMain_ui.c
    ui_design/user/DashboardMain_user.c
)

set(private_includes)
ameba_list_append(private_includes
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ui_design
    ${CMAKE_CURRENT_SOURCE_DIR}/ui_design/callbacks
    ${CMAKE_CURRENT_SOURCE_DIR}/ui_design/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/ui_design/user
    ${honeygui_includes}
)

set(private_compile_options)
ameba_list_append(private_compile_options
    -include ${HONEYGUI_DIR}/port/gui_port_init.h
)

ameba_add_internal_library(honeygui_app_dashboard
    p_SOURCES ${private_sources}
    p_INCLUDES ${private_includes}
    p_COMPILE_OPTIONS ${private_compile_options}
)
