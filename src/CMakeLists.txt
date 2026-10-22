#
# Copyright (c) 2026, Realtek Semiconductor Corporation
#
# SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
#

cmake_minimum_required(VERSION 3.10)

# src/ holds no sources itself; it only pulls in each sub-module (currently ble/)
# to the app target. The top-level CMakeLists.txt scans subdirectories only when a
# CMakeLists.txt is present here, otherwise it prints "Skipping directory src" and
# silently drops the whole src/ subtree from the build.
if(1)
    file(GLOB SOURCES "*.c" "*.cpp")
    file(GLOB HEADERS "*.h" "*.hpp")

    if(SOURCES)
        target_sources(${APP_TARGET_NAME} PRIVATE ${SOURCES})
    endif()

    if(HEADERS)
        target_include_directories(${APP_TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    endif()

    # Process subdirectories regardless
    file(GLOB SUBDIRS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/*)

    foreach(subdir ${SUBDIRS})
        if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${subdir})
            if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${subdir}/CMakeLists.txt)
                add_subdirectory(${subdir})
            else()
                message(STATUS "Skipping directory src/${subdir} as it does not contain a CMakeLists.txt file.")
            endif()
        endif()
    endforeach()
endif()
