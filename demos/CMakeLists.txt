# HoneyGUI Demos CMakeLists.txt

set(private_sources)
ameba_list_append(private_sources
    honeygui_demos.c
)

# c_CURRENT_IMAGE already contains the full suffix like "target_img2_km4tz"
set(gui_target_name honeygui_${c_CURRENT_IMAGE})

# Get include directories from honeygui target directly
get_target_property(honeygui_includes ${gui_target_name} INTERFACE_INCLUDE_DIRECTORIES)

set(private_includes)
ameba_list_append(private_includes
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${honeygui_includes}
)

ameba_add_internal_library(honeygui_demos
    p_SOURCES ${private_sources}
    p_INCLUDES ${private_includes}
)
