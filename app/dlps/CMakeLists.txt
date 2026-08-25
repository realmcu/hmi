# Copyright (c) 2026, Realtek Semiconductor Corporation
# SPDX-License-Identifier: LicenseRef-Realtek-5-Clause

cmake_minimum_required(VERSION 3.10)

target_sources(${APP_TARGET_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/app_dlps.c
)

target_include_directories(${APP_TARGET_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
