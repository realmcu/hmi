# Device adapters for HoneyGUI system values, parallel to win32_sim/port/realgui_port/sysval.
target_sources(${APP_TARGET_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/gui_port_sysval_time.c
    ${CMAKE_CURRENT_SOURCE_DIR}/gui_port_sysval_pedometer.c
    ${CMAKE_CURRENT_SOURCE_DIR}/gui_port_sysval_power.c
    ${CMAKE_CURRENT_SOURCE_DIR}/gui_port_sysval_connectivity.c
    ${CMAKE_CURRENT_SOURCE_DIR}/gui_port_sysval_device.c
)
target_include_directories(${APP_TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
