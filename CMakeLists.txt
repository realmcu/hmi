# hmi-component - HoneyGUI platform integration modules
# Contains: port (platform porting), demos, acc (acceleration), app (applications)

ameba_add_subdirectory_if_exist(port)
ameba_add_subdirectory_if_exist(demos)
ameba_add_subdirectory_if_exist(acc)

# app subdirectory needs gui_port_init.h for GUI_INIT_*_EXPORT constructor registration
set(_gui_saved_c_flags "${CMAKE_C_FLAGS}")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -include ${CMAKE_CURRENT_SOURCE_DIR}/port/gui_port_init.h")
ameba_add_subdirectory_if_exist(app)
set(CMAKE_C_FLAGS "${_gui_saved_c_flags}")
