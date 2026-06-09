# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)

# Force the board-specific overlay that provides the wifi_int alias/node.
set(DTC_OVERLAY_FILE
    ${CMAKE_CURRENT_SOURCE_DIR}/boards/rtl87x3g_watch_rtl8783gbf.overlay)

# Keep the standard Realtek VERSION file expected by post_build.cmake.
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/version_generate.cmake)
generate_version()

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/post_build.cmake)
rtk_post_build()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(z2plusTCPUDP)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/mp.ini ${CMAKE_BINARY_DIR}/../mp.ini COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/VERSION ${CMAKE_BINARY_DIR}/../VERSION COPYONLY)

target_sources(app PRIVATE
    src/main.c
)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# SDIO DMA buffers and card state must live in SRAM instead of the default DTCM.
zephyr_code_relocate(FILES
    src/main.c
    LOCATION SRAM_BSS_DATA NOKEEP)