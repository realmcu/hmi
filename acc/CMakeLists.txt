# HoneyGUI Port Layer CMakeLists.txt for Ameba Platform

set(private_sources)
ameba_list_append(private_sources
    acc_jpeg.c
    acc_ppe.c
    rtl_ppe.c
)

# c_CURRENT_IMAGE already contains the full suffix like "target_img2_km4tz"
set(gui_target_name honeygui_${c_CURRENT_IMAGE})

# Get include directories from honeygui target directly
get_target_property(honeygui_includes ${gui_target_name} INTERFACE_INCLUDE_DIRECTORIES)

set(private_includes)
ameba_list_append(private_includes
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${honeygui_includes}
    ${c_CMPT_FWLIB_DIR}/jpeg_decoder/inc
)

ameba_add_internal_library(honeygui_acc
    p_SOURCES ${private_sources}
    p_INCLUDES ${private_includes}
)
