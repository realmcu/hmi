# Copyright (c) 2026 Realtek Semiconductor Corp.
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.10)

file(GLOB SOURCES "*.c" "*.cpp")
file(GLOB HEADERS "*.h" "*.hpp")

if(SOURCES)
    target_sources(${APP_TARGET_NAME} PRIVATE ${SOURCES})
endif()
if(HEADERS)
    target_include_directories(${APP_TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()
