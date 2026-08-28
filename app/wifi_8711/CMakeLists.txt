# Copyright (c) 2026 Realtek Semiconductor Corp.
# SPDX-License-Identifier: Apache-2.0

# Guarded on CONFIG_WIFI_8711_ROLE_SLAVE, NOT on CONFIG_WIFI_8711.
#
# The distinction is the whole point of the guard.  WIFI_8711 now means "an
# external 8711 exists" and is on for the master role too, where this directory
# must NOT be compiled: its sources bind to spi0_slave and friends, nodes the
# wifi overlay does not provide, so a build of the master arrangement would fail
# in the DT layer with an error naming a role nobody asked for.  ROLE_SLAVE
# (default n) is the symbol that means "compile the retired slave stack", and it
# comes with the wifi_8711 snippet that supplies those nodes.
#
# NOTE: file(GLOB) means CMake does not notice a newly added .c file on an
# incremental build.  Add a source here -> rebuild pristine (build.ps1
# -Pristine), same trap as app/protocol/CMakeLists.txt.

if(CONFIG_WIFI_8711_ROLE_SLAVE)

message(STATUS "CMAKELIST FOLDER: ${CMAKE_CURRENT_SOURCE_DIR}")

file(GLOB SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.c")

target_sources(${APP_TARGET_NAME} PRIVATE ${SOURCES})
target_include_directories(${APP_TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

endif()
