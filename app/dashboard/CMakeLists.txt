# Dashboard APP sources

set(gui_target_name honeygui_${c_CURRENT_IMAGE})

get_target_property(honeygui_includes ${gui_target_name} INTERFACE_INCLUDE_DIRECTORIES)

get_filename_component(HONEYGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../.. ABSOLUTE)

set(private_sources)
ameba_list_append(private_sources
    src/dashboard_main.c
    ui_design/src/dashboardEntry.c
    ui_design/src/callbacks/DashboardMain_callbacks.c
    ui_design/src/ui/DashboardMain_ui.c
    ui_design/src/user/DashboardMain_user.c
)

if(CONFIG_BT)
    ameba_list_append(private_sources
        ble/dashboard_ble.c
        ble/dashboard_hid_service/dashboard_hids_cc.c
    )
endif()

if(CONFIG_BT_EXT)
    ameba_list_append(private_sources
        bt_ext/dashboard_bt_ext.c
        bt_ext/ble/dashboard_ble.c
        bt_ext/ble/bt_ext_hids_cc.c
        bt_ext/bt_classic/dashboard_bt_classic.c
        bt_ext/bt_classic/bt_classic_link.c
        bt_ext/bt_classic/bt_classic_a2dp.c
        bt_ext/bt_classic/bt_classic_avrcp.c
        bt_ext/bt_classic/bt_classic_hfp.c
        bt_ext/bt_classic/bt_classic_sdp.c
        bt_ext/bt_classic/bt_classic_spp.c
        bt_ext/bt_classic/bt_classic_hid.c
    )
endif()

# Key input (GPIO buttons + CLI) feeds both BLE stacks; compile it once when
# either stack is enabled.
if(CONFIG_BT OR CONFIG_BT_EXT)
    ameba_list_append(private_sources
        io_peripheral/dashboard_key.c
    )
endif()

set(THIRD_PARTY_NAV_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party_nav)
file(GLOB THIRD_PARTY_NAV_LIBS ${THIRD_PARTY_NAV_DIR}/lib/*.a)
set(DASHBOARD_USE_THIRD_PARTY_NAV FALSE)
if(CONFIG_DASHBOARD_THIRD_PARTY_NAV AND THIRD_PARTY_NAV_LIBS)
    set(DASHBOARD_USE_THIRD_PARTY_NAV TRUE)
endif()

if(CONFIG_WLAN)
    ameba_list_append(private_sources
        wifi/dashboard_wifi.c
        wifi/dashboard_ota_http.c
        wifi/dashboard_img_rx.c
        wifi/dashboard_img_display.c
    )
    if(DASHBOARD_USE_THIRD_PARTY_NAV)
        ameba_list_append(private_sources
            third_party_nav/dashboard_third_party_nav.c
            third_party_nav/dashboard_third_party_nav_view.c
        )
        set_source_files_properties(
            third_party_nav/dashboard_third_party_nav.c
            PROPERTIES COMPILE_OPTIONS "-Wno-error;-Wno-strict-prototypes"
        )
    endif()
endif()

set(private_includes)
ameba_list_append(private_includes
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/ui_design/src
    ${CMAKE_CURRENT_SOURCE_DIR}/ui_design/src/callbacks
    ${CMAKE_CURRENT_SOURCE_DIR}/ui_design/src/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/ui_design/src/user
    ${CMAKE_CURRENT_SOURCE_DIR}/ui_design/callbacks
    ${CMAKE_CURRENT_SOURCE_DIR}/ui_design/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/ui_design/user
    ${CMAKE_CURRENT_SOURCE_DIR}/ble
    ${CMAKE_CURRENT_SOURCE_DIR}/ble/dashboard_hid_service
    ${CMAKE_CURRENT_SOURCE_DIR}/io_peripheral
    ${CMAKE_CURRENT_SOURCE_DIR}/wifi
    ${CMAKE_CURRENT_SOURCE_DIR}/bt_ext
    ${CMAKE_CURRENT_SOURCE_DIR}/bt_ext/ble
    ${CMAKE_CURRENT_SOURCE_DIR}/bt_ext/bt_classic
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../bluetooth/example/gatt_service/server
    ${honeygui_includes}
)

if(DASHBOARD_USE_THIRD_PARTY_NAV)
    ameba_list_append(private_includes
        ${THIRD_PARTY_NAV_DIR}
        ${THIRD_PARTY_NAV_DIR}/inc
    )
    foreach(_tpnav_lib ${THIRD_PARTY_NAV_LIBS})
        ameba_global_library(${_tpnav_lib})
    endforeach()
endif()

set(private_compile_options)
ameba_list_append(private_compile_options
    -include ${HONEYGUI_DIR}/port/gui_port_init.h
)

set(private_definitions)
if(DASHBOARD_USE_THIRD_PARTY_NAV)
    ameba_list_append(private_definitions DASHBOARD_USE_THIRD_PARTY_NAV)
endif()

ameba_add_internal_library(honeygui_app_dashboard
    p_SOURCES ${private_sources}
    p_INCLUDES ${private_includes}
    p_DEFINITIONS ${private_definitions}
    p_COMPILE_OPTIONS ${private_compile_options}
)
