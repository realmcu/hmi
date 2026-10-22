# Copyright (c) 2024, Realtek Semiconductor Corporation.
# SPDX-License-Identifier: Apache-2.0
#
# FlashDB port layer CMakeLists
#
# This file is responsible for:
#   1. Pulling in the FlashDB core sources (upstream repository, read-only)
#   2. Pulling in the FAL abstraction layer (upstream port/fal/, read-only)
#   3. Compiling the RTK platform port files kept in this directory
#   4. Exporting all required include directories to the app target
#
# Discovered and added by the sub-directory scan in port/CMakeLists.txt.

cmake_minimum_required(VERSION 3.20.0)

# FlashDB sources location (port/flashdb/ -> component/FlashDB/)
set(FLASHDB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../component/FlashDB)

# The component/ directory is not part of this application yet (the FlashDB
# repository is not vendored here). Skip the whole port when the sources are
# missing, so CMake does not fail with "Cannot find source file".
# Once FlashDB is placed under component/FlashDB this block activates on its own.
if(NOT EXISTS ${FLASHDB_DIR}/src/fdb.c)
    message(STATUS "port/flashdb: skip (missing ${FLASHDB_DIR})")
    return()
endif()

# ============================================================
# 1. FlashDB core sources (upstream repository, read-only)
# ============================================================
target_sources(${APP_TARGET_NAME} PRIVATE
    ${FLASHDB_DIR}/src/fdb.c
    ${FLASHDB_DIR}/src/fdb_kvdb.c
    ${FLASHDB_DIR}/src/fdb_tsdb.c
    ${FLASHDB_DIR}/src/bf/fdb_bf.c
    ${FLASHDB_DIR}/src/fdb_utils.c
)

target_include_directories(${APP_TARGET_NAME} PUBLIC
    ${FLASHDB_DIR}/inc   # flashdb.h / fdb_def.h / fdb_low_lvl.h
)

# ============================================================
# 2. FAL abstraction layer sources (from the FlashDB repository, read-only)
# ============================================================
set(FAL_DIR ${FLASHDB_DIR}/port/fal)

target_sources(${APP_TARGET_NAME} PRIVATE
    ${FAL_DIR}/src/fal.c
    ${FAL_DIR}/src/fal_flash.c
    ${FAL_DIR}/src/fal_partition.c
    # fal_rtt.c is RT-Thread specific and is not used on Zephyr
)

target_include_directories(${APP_TARGET_NAME} PUBLIC
    ${FAL_DIR}/inc       # fal.h / fal_def.h
)

# ============================================================
# 3. RTK platform port files + configuration headers (maintained here)
#    fal_flash_rtk_port.c  - flash driver built on the Realtek FMC API
#    fdb_cfg.h             - FlashDB feature configuration
#    fal_cfg.h             - FAL device table + partition table
# ============================================================
target_sources(${APP_TARGET_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/fal_flash_rtk_port.c
    ${CMAKE_CURRENT_SOURCE_DIR}/flashdb_test.c
)

# fmc_api.h location (Realtek HAL module)
set(FMC_API_INC_DIR
    ${ZEPHYR_BASE}/../modules/hal/realtek/rtl87x3g/platform/hal_lib/inc/platform)

# BEFORE guarantees this directory wins over FlashDB/inc, so that
# fdb_cfg.h / fal_cfg.h override the template files shipped with the repository
target_include_directories(${APP_TARGET_NAME} BEFORE PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${FMC_API_INC_DIR}
)

message(STATUS "FlashDB: port/flashdb integrated (KVDB + FAL + RTK port)")
