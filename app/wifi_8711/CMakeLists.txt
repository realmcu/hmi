# Copyright (c) 2026 Realtek Semiconductor Corp.
# SPDX-License-Identifier: Apache-2.0

# Guarded on CONFIG_WIFI_8711 so a build without the wifi_8711 snippet does
# not compile any of this -- the DT nodes it binds to only exist there.
#
# NOTE: file(GLOB) means CMake does not notice a newly added .c file on an
# incremental build.  Add a source here -> rebuild pristine (build.ps1
# -Pristine), same trap as app/protocol/CMakeLists.txt.

if(CONFIG_WIFI_8711)

message(STATUS "CMAKELIST FOLDER: ${CMAKE_CURRENT_SOURCE_DIR}")

file(GLOB SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.c")

target_sources(${APP_TARGET_NAME} PRIVATE ${SOURCES})
target_include_directories(${APP_TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

endif()
