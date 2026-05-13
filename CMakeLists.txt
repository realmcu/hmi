if (CONFIG_HONEYGUI_ENABLE)
    set(_gui_saved_c_flags "${CMAKE_C_FLAGS}")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -include ${CMAKE_CURRENT_SOURCE_DIR}/port/gui_port_init.h")
    ameba_add_subdirectory(honeygui)
    set(CMAKE_C_FLAGS "${_gui_saved_c_flags}")

    add_library(honeygui_interface INTERFACE)
    target_link_libraries(honeygui_interface INTERFACE ${GUI_TARGET_NAME})

    if(c_CURRENT_IMAGE)
        target_link_options(${c_CURRENT_IMAGE} PRIVATE "-Wl,--wrap=gui_components_init")
    endif()

    ameba_add_subdirectory_if_exist(port)
    ameba_add_subdirectory_if_exist(demos)
    ameba_add_subdirectory_if_exist(acc)

    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -include ${CMAKE_CURRENT_SOURCE_DIR}/port/gui_port_init.h")
    ameba_add_subdirectory_if_exist(app)
    set(CMAKE_C_FLAGS "${_gui_saved_c_flags}")
endif()
