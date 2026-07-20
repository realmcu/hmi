# Copyright (c) 2026 Realtek Semiconductor Corp.
# SPDX-License-Identifier: Apache-2.0
#
# BLE OTA (DFU) module ported from samples/ota into eBadge_8773g.
# Compiled into the Zephyr `app` target (APP_TARGET_NAME is set by the top CMakeLists).

cmake_minimum_required(VERSION 3.10)

target_sources(${APP_TARGET_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/app_ota.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ota_service.c
    ${CMAKE_CURRENT_SOURCE_DIR}/app_ota_glue.c
    ${CMAKE_CURRENT_SOURCE_DIR}/app_ota_service_cb.c
)

# app/ota own headers + the copied compat shims (app_main.h/app_cfg.h/app_link_util.h/app_ble_gap.h).
# PUBLIC so other eBadge code (e.g. hmi_ble_profile_init.c) can include app_ota_service.h.
target_include_directories(${APP_TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# BLE-DFU only for this port (no SPP / BR-EDR OTA).
target_compile_definitions(${APP_TARGET_NAME} PRIVATE APP_OTA_SPP_SUPPORT=0)
