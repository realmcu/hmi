# Copyright (c) 2024, Realtek Semiconductor Corporation.
# SPDX-License-Identifier: Apache-2.0
#
# FlashDB port layer CMakeLists -- eBadge platform
#
# This file is responsible for:
#   1. Pulling in the FlashDB core sources (from the standalone West project, unmodified)
#   2. Pulling in the FAL abstraction layer sources (port/fal/ from the FlashDB repo)
#   3. Compiling the RTK platform port files in this directory
#   4. Injecting all required header paths into the app target
#
# Auto-discovered and add_subdirectory'd by the foreach subdirectory scan in port/CMakeLists.txt.

cmake_minimum_required(VERSION 3.20.0)

# FlashDB West project path (port/flashdb/ -> eBadge/ -> FlashDB/)
set(FLASHDB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../component/FlashDB)

# ============================================================
# 1. FlashDB core sources (the repo itself, read-only, unmodified)
# ============================================================
target_sources(${APP_TARGET_NAME} PRIVATE
    ${FLASHDB_DIR}/src/fdb.c
    ${FLASHDB_DIR}/src/fdb_kvdb.c
    ${FLASHDB_DIR}/src/fdb_utils.c
    ${FLASHDB_DIR}/src/bf/fdb_bf.c   # BigFile large-file extension (FDB_USING_BF)
    # For a time-series database, uncomment:
    # ${FLASHDB_DIR}/src/fdb_tsdb.c
)

target_include_directories(${APP_TARGET_NAME} PUBLIC
    ${FLASHDB_DIR}/inc   # flashdb.h / fdb_def.h / fdb_low_lvl.h
)

# ============================================================
# 2. FAL abstraction layer sources (from the FlashDB repo, read-only, unmodified)
# ============================================================
set(FAL_DIR ${FLASHDB_DIR}/port/fal)

target_sources(${APP_TARGET_NAME} PRIVATE
    ${FAL_DIR}/src/fal.c
    ${FAL_DIR}/src/fal_flash.c
    ${FAL_DIR}/src/fal_partition.c
    # fal_rtt.c is RT-Thread specific; not pulled in for Zephyr
)

target_include_directories(${APP_TARGET_NAME} PUBLIC
    ${FAL_DIR}/inc       # fal.h / fal_def.h
)

# ============================================================
# 3. RTK platform port files + configuration headers (this directory, maintained by the app project)
#    fal_flash_rtk_port.c  -- Flash driver, using the Realtek FMC API
#    fdb_cfg.h             -- FlashDB feature configuration
#    fal_cfg.h             -- FAL device table + partition table
# ============================================================
target_sources(${APP_TARGET_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/fal_flash_rtk_port.c
)

# fmc_api.h path (Realtek HAL module)
set(FMC_API_INC_DIR
    ${ZEPHYR_BASE}/../modules/hal/realtek/rtl87x3g/platform/hal_lib/inc/platform)

# BEFORE makes this directory's headers take precedence over FlashDB/inc,
# so fdb_cfg.h / fal_cfg.h can override the template files in the repo
target_include_directories(${APP_TARGET_NAME} BEFORE PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${FMC_API_INC_DIR}
)

message(STATUS "FlashDB: port/flashdb integrated (KVDB + BF + FAL + RTK port)")
