#
# Copyright (c) 2026, Realtek Semiconductor Corporation
#
# SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
#

cmake_minimum_required(VERSION 3.10)

# System-level helpers for the application target: the log backend that forwards
# Zephyr log messages to the Realtek log channel, and friends.
file(GLOB SOURCES "*.c" "*.cpp")
file(GLOB HEADERS "*.h" "*.hpp")

if(SOURCES)
    target_sources(${APP_TARGET_NAME} PRIVATE ${SOURCES})
endif()

if(HEADERS)
    target_include_directories(${APP_TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()
