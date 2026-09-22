# Copyright (c) 2026 Realtek Semiconductor Corp.
# SPDX-License-Identifier: Apache-2.0
#
# watch_ui（HoneyGUI Designer 产品 UI，smartwatch_ui_360360）的挂载点。
#
# 和 component/ 下那些 west project 不同：watch_ui 在 manifest 里 path=ui，
# 是 applications 的兄弟目录而非子目录，顶层 CMakeLists 的 GLOB 子目录循环
# 扫不到，所以这里显式 add_subdirectory 把它拉进来。
#
# 路径锚在 WEST_TOPDIR（由 find_package(Zephyr) 内的 cmake/modules/west.cmake
# 执行 `west topdir` 得到，本文件在 find_package 之后处理，故可用）。不写成
# $ENV{ZEPHYR_BASE}/../../ui —— 后者隐含"zephyr 在 topdir 下两层"的假设，
# 而那两层只是 manifest 里 path-prefix: zephyrproject 的产物，manifest 一改
# 就失效。
#
# ui 仓自带的 CMakeLists.txt 已经按本仓约定写好（依赖外部定义的
# APP_TARGET_NAME，再 target_sources 到 app 上），因此不必在 port/ 里重复挑
# 源码——直接复用它即可。这也保证 Designer 那边改动 ui 仓时，applications
# 侧零改动。
#
# 源码目录在本工程之外，add_subdirectory 必须显式给 binary dir。

cmake_minimum_required(VERSION 3.10)

if(CONFIG_WATCH_UI)
    if(NOT WEST_TOPDIR)
        message(FATAL_ERROR
            "CONFIG_WATCH_UI=y needs WEST_TOPDIR to locate the watch_ui project, "
            "but west is unavailable -- build through 'west build', not bare cmake."
        )
    endif()

    set(WATCH_UI_ROOT ${WEST_TOPDIR}/ui)

    if(NOT EXISTS ${WATCH_UI_ROOT}/CMakeLists.txt)
        message(FATAL_ERROR
            "CONFIG_WATCH_UI=y but the watch_ui project is missing at "
            "${WATCH_UI_ROOT} -- run 'west update' to fetch it."
        )
    endif()

    message(STATUS "[watch_ui] mounting product UI from ${WATCH_UI_ROOT}")
    add_subdirectory(${WATCH_UI_ROOT} ${CMAKE_BINARY_DIR}/watch_ui)
else()
    message(STATUS "[watch_ui] CONFIG_WATCH_UI=n, product UI not built")
endif()
