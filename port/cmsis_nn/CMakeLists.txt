# Copyright (c) 2026, Realtek Semiconductor Corporation.
# SPDX-License-Identifier: Apache-2.0
#
# CMSIS-NN port —— eBadge / RTL8773G 平台 (Cortex-M55 + Helium MVE)
#
# 依赖: 无 (CMSIS-NN v6.0.0 header 只依赖标准 C, 不需要 CMSIS-DSP)
# MVE 启用: CMSIS-NN 内部靠 __ARM_FEATURE_MVE 宏自动切分支,
#          这个宏由 -mcpu=cortex-m55 编译器自动定义, 无需手工加 flag.
#
# 目的: 让 TFLM 的 kernels/cmsis_nn/*.cc 拿到 arm_nn* 符号,
#      从而在 int8 推理时走 CMSIS-NN 的 MVE 优化 kernel.

cmake_minimum_required(VERSION 3.20.0)

# 通过 CONFIG_APP_CMSIS_NN 控制是否编译 (见 simple-test/Kconfig)
# 关掉此项时 TFLM 的 FullyConnected 会回落到 reference kernel
# (需要同步把 port/tflite_micro/CMakeLists.txt 里的
#  kernels/cmsis_nn/fully_connected.cc 换回 kernels/fully_connected.cc)
if(NOT CONFIG_APP_CMSIS_NN)
    return()
endif()

set(CMSIS_NN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../component/cmsis-nn)

# ============================================================
# 1. CMSIS-NN 全部 kernel 源码 (99 个 .c 文件)
#    Source/ 下只有实现文件, 没有 test/example 混入, 可安全 GLOB.
# ============================================================
file(GLOB_RECURSE CMSIS_NN_SRCS "${CMSIS_NN_DIR}/Source/*.c")

target_sources(${APP_TARGET_NAME} PRIVATE ${CMSIS_NN_SRCS})

# ============================================================
# 2. Include 路径
#    - CMSIS-NN 根目录: 让 TFLM 的 "Include/arm_nnfunctions.h" 能找到
#    - Include/ 本身: CMSIS-NN 内部源码用 "arm_nnfunctions.h" 无前缀 include
# ============================================================
target_include_directories(${APP_TARGET_NAME} PUBLIC
    ${CMSIS_NN_DIR}
    ${CMSIS_NN_DIR}/Include
)

# ============================================================
# 3. CMSIS-NN 编译 flag
#    - 关掉 C 里几个会 warn-as-error 的项 (CMSIS-NN 有 unused var/param)
# ============================================================
set_source_files_properties(
    ${CMSIS_NN_SRCS}
    PROPERTIES
    COMPILE_FLAGS "-Wno-unused-parameter -Wno-unused-variable -Wno-sign-compare"
)

# ============================================================
# 4. 关键宏 CMSIS_NN
#    TFLM 的 kernels/fully_connected.h 靠这个宏在
#    "reference 内联" 和 "cmsis_nn 外部实现" 之间分派 —— 必须定义,
#    否则 kernels/cmsis_nn/fully_connected.cc 会跟 header 里的
#    inline 版本重复定义, link 时 (或早在 compile 时) 就炸.
#
#    加到全工程 —— 只影响 TFLM header, 其他代码里没用这个名字.
# ============================================================
zephyr_compile_definitions(CMSIS_NN)

message(STATUS "CMSIS-NN: v6.0.0 integrated, ${CMSIS_NN_DIR}")
