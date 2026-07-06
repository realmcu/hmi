# Copyright (c) 2024, Realtek Semiconductor Corporation.
# SPDX-License-Identifier: Apache-2.0
#
# FlashDB 移植层 CMakeLists —— eBadge 平台
#
# 此文件负责：
#   1. 引入 FlashDB 核心源码（来自独立 West project，不修改）
#   2. 引入 FAL 抽象层源码（来自 FlashDB 仓库的 port/fal/）
#   3. 编译本目录下的 RTK 平台移植文件
#   4. 将所有必要头文件路径注入 app target
#
# 被 port/CMakeLists.txt 的 foreach 子目录扫描自动发现并 add_subdirectory。

cmake_minimum_required(VERSION 3.20.0)

# FlashDB West project 路径（port/flashdb/ → eBadge/ → FlashDB/）
set(FLASHDB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../component/FlashDB)

# ============================================================
# 1. FlashDB 核心源码（仓库本身，只读，不修改）
# ============================================================
target_sources(${APP_TARGET_NAME} PRIVATE
    ${FLASHDB_DIR}/src/fdb.c
    ${FLASHDB_DIR}/src/fdb_kvdb.c
    ${FLASHDB_DIR}/src/fdb_utils.c
    # 如需时序数据库，取消注释：
    # ${FLASHDB_DIR}/src/fdb_tsdb.c
)

target_include_directories(${APP_TARGET_NAME} PUBLIC
    ${FLASHDB_DIR}/inc   # flashdb.h / fdb_def.h / fdb_low_lvl.h
)

# ============================================================
# 2. FAL 抽象层源码（来自 FlashDB 仓库，只读，不修改）
# ============================================================
set(FAL_DIR ${FLASHDB_DIR}/port/fal)

target_sources(${APP_TARGET_NAME} PRIVATE
    ${FAL_DIR}/src/fal.c
    ${FAL_DIR}/src/fal_flash.c
    ${FAL_DIR}/src/fal_partition.c
    # fal_rtt.c 为 RT-Thread 专用，Zephyr 不引入
)

target_include_directories(${APP_TARGET_NAME} PUBLIC
    ${FAL_DIR}/inc       # fal.h / fal_def.h
)

# ============================================================
# 3. RTK 平台移植文件 + 配置头文件（本目录，应用工程维护）
#    fal_flash_rtk_port.c  — Flash 驱动，使用 Realtek FMC API
#    fdb_cfg.h             — FlashDB 功能配置
#    fal_cfg.h             — FAL 设备表 + 分区表
# ============================================================
target_sources(${APP_TARGET_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/fal_flash_rtk_port.c
    ${CMAKE_CURRENT_SOURCE_DIR}/flashdb_test.c
)

# fmc_api.h 路径（Realtek HAL 模块）
set(FMC_API_INC_DIR
    ${ZEPHYR_BASE}/../modules/hal/realtek/rtl87x3g/platform/hal_lib/inc/platform)

# BEFORE 确保本目录头文件优先于 FlashDB/inc，
# 使 fdb_cfg.h / fal_cfg.h 能覆盖仓库内的模板文件
target_include_directories(${APP_TARGET_NAME} BEFORE PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${FMC_API_INC_DIR}
)

message(STATUS "FlashDB: port/flashdb integrated (KVDB + FAL + RTK port)")
