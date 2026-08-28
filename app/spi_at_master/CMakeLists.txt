# Copyright (c) 2026 Realtek Semiconductor Corp.
# SPDX-License-Identifier: Apache-2.0

# Guarded on CONFIG_SPI_AT_MASTER so a build without the spi_at_master snippet
# does not compile any of this -- the DT nodes it binds to only exist there.
#
# NOTE: file(GLOB) means CMake does not notice a newly added .c file on an
# incremental build.  Add a source here -> rebuild pristine (build.ps1
# -Pristine), same trap as app/wifi_8711/CMakeLists.txt.

if(CONFIG_SPI_AT_MASTER)

message(STATUS "CMAKELIST FOLDER: ${CMAKE_CURRENT_SOURCE_DIR}")

file(GLOB SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.c")

target_sources(${APP_TARGET_NAME} PRIVATE ${SOURCES})
target_include_directories(${APP_TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

endif()
