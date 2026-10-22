#
# Copyright (c) 2026, Realtek Semiconductor Corporation
#
# SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
#
# BLE application layer: GAP / ADV / GATT service / task plus the navigation
# projection custom service.
#
# Depends on the gatt_profile shipped with the realtek-app repository (outside this
# project, provided through the Zephyr module mechanism):
#   subsys/bluetooth/gatt_profile/src/server/{bas,dis,simple_ble_service}.c
#   subsys/bluetooth/gatt_profile/inc/{bt_gatt_client,bt_gatt_svc,client,server}
# The wiring follows realtek-app/samples/ble_peripheral/CMakeLists.txt.
#
# Note: the ANCS client (ancs_client.c / ancs_sample.c) is not enabled right now,
# because app_ble_flags.h sets F_APP_BT_GATT_CLIENT_SUPPORT = 0. Enabling that switch
# also requires ancs_client.c and samples/ble_peripheral/src/ancs_sample.{c,h}.
if(1)
    file(GLOB SOURCES "*.c" "*.cpp")
    file(GLOB HEADERS "*.h" "*.hpp")

    if(SOURCES AND (NOT HEADERS))
        target_sources(${APP_TARGET_NAME} PRIVATE ${SOURCES})
    endif()

    if(SOURCES AND HEADERS)
        target_sources(${APP_TARGET_NAME} PRIVATE ${SOURCES})
        target_include_directories(${APP_TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    endif()
endif()

# ----------------------------------------------------------------------------
# Generic GATT profiles from realtek-app (BAS / DIS / Simple BLE Service).
# app_ble_service.h includes <simple_ble_service.h> and <bas.h> directly, so the
# header directories have to be exported and the sources have to be compiled here.
# ----------------------------------------------------------------------------
set(RTK_GATT_PROFILE_DIR ${ZEPHYR_REALTEK_APP_MODULE_DIR}/subsys/bluetooth/gatt_profile)

if(NOT EXISTS ${RTK_GATT_PROFILE_DIR}/src/server/simple_ble_service.c)
    message(FATAL_ERROR "gatt_profile not found: ${RTK_GATT_PROFILE_DIR}. "
                        "Make sure realtek-app is registered as a Zephyr module (west.yml).")
endif()

target_sources(${APP_TARGET_NAME} PRIVATE
    ${RTK_GATT_PROFILE_DIR}/src/server/bas.c
    ${RTK_GATT_PROFILE_DIR}/src/server/dis.c
    ${RTK_GATT_PROFILE_DIR}/src/server/simple_ble_service.c
)

target_include_directories(${APP_TARGET_NAME} PUBLIC
    ${RTK_GATT_PROFILE_DIR}/inc/bt_gatt_client
    ${RTK_GATT_PROFILE_DIR}/inc/bt_gatt_svc
    ${RTK_GATT_PROFILE_DIR}/inc/client
    ${RTK_GATT_PROFILE_DIR}/inc/server
)
