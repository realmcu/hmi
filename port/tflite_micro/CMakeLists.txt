# Copyright (c) 2026, Realtek Semiconductor Corporation.
# SPDX-License-Identifier: Apache-2.0
#
# TFLite Micro port —— eBadge / RTL8773G 平台
#
# 参考 port/flashdb/CMakeLists.txt 的风格：
#   1. 源码来自 west 拉下的 component/tflite-micro/ 只读仓库
#   2. 本目录负责精确列出编译哪些源、开启哪些 include、加什么编译 flag
#   3. hello_world 模型 .tflite 需要先转成 .cc/.h（xxd 或脚本）
#
# 第一版目标：跑通 hello_world_float，只用 FullyConnected 一个算子。

cmake_minimum_required(VERSION 3.20.0)

# 通过 CONFIG_APP_TFLITE_MICRO 控制是否编译 (见 simple-test/Kconfig)
if(NOT CONFIG_APP_TFLITE_MICRO)
    return()
endif()

# ============================================================
# 路径
# ============================================================
set(TFLM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../component/tflite-micro)
set(TFLM_TF   ${TFLM_DIR}/tensorflow)
set(TFLM_MICRO ${TFLM_TF}/lite/micro)
set(TFLM_TP   ${TFLM_DIR}/third_party_static)   # 使用 exported 版本

# ============================================================
# 收集全部 TFLM 相关源文件到 TFLM_SOURCES，
# 统一 target_sources() 一次 + 统一 set_source_files_properties() 挂 flag，
# 这样 -std=gnu++17 / -fno-exceptions / -Wno-* 只作用于 TFLM 自己的 .cc，
# 不会污染 kernel / guiLib / 其他 C++ 代码。
# ============================================================

# 1. Micro 运行时核心（tensorflow/lite/micro/*.cc）
#    只挑运行 hello_world 必要的；test/mock/fake/recording 全排除
set(TFLM_SOURCES
    ${TFLM_MICRO}/debug_log.cc
    ${TFLM_MICRO}/flatbuffer_utils.cc
    ${TFLM_MICRO}/memory_helpers.cc
    ${TFLM_MICRO}/micro_allocation_info.cc
    ${TFLM_MICRO}/micro_allocator.cc
    ${TFLM_MICRO}/micro_context.cc
    ${TFLM_MICRO}/micro_interpreter.cc
    ${TFLM_MICRO}/micro_interpreter_context.cc
    ${TFLM_MICRO}/micro_interpreter_graph.cc
    ${TFLM_MICRO}/micro_log.cc
    ${TFLM_MICRO}/micro_op_resolver.cc
    ${TFLM_MICRO}/micro_resource_variable.cc
    ${TFLM_MICRO}/micro_time.cc
    ${TFLM_MICRO}/micro_utils.cc
    ${TFLM_MICRO}/system_setup.cc
    # tflite_bridge/ —— flatbuffer -> TFLite 参数转换（core/api 到 micro 的桥）
    ${TFLM_MICRO}/tflite_bridge/flatbuffer_conversions_bridge.cc
    ${TFLM_MICRO}/tflite_bridge/micro_error_reporter.cc

    # 2. Arena allocator（micro/arena_allocator/）
    ${TFLM_MICRO}/arena_allocator/non_persistent_arena_buffer_allocator.cc
    ${TFLM_MICRO}/arena_allocator/persistent_arena_buffer_allocator.cc
    ${TFLM_MICRO}/arena_allocator/recording_single_arena_buffer_allocator.cc
    ${TFLM_MICRO}/arena_allocator/single_arena_buffer_allocator.cc

    # 3. Memory planner（micro/memory_planner/）
    ${TFLM_MICRO}/memory_planner/greedy_memory_planner.cc
    ${TFLM_MICRO}/memory_planner/linear_memory_planner.cc
    ${TFLM_MICRO}/memory_planner/non_persistent_buffer_planner_shim.cc

    # 4. TFLite core / kernels 通用
    ${TFLM_TF}/lite/core/c/common.cc
    ${TFLM_TF}/lite/core/api/error_reporter.cc
    ${TFLM_TF}/lite/core/api/flatbuffer_conversions.cc
    ${TFLM_TF}/lite/core/api/tensor_utils.cc
    ${TFLM_TF}/lite/kernels/internal/common.cc
    ${TFLM_TF}/lite/kernels/internal/portable_tensor_utils.cc
    ${TFLM_TF}/lite/kernels/internal/quantization_util.cc
    ${TFLM_TF}/lite/kernels/internal/runtime_shape.cc
    ${TFLM_TF}/lite/kernels/internal/tensor_ctypes.cc
    ${TFLM_TF}/lite/kernels/internal/tensor_utils.cc
    ${TFLM_TF}/lite/kernels/kernel_util.cc
    ${TFLM_TF}/lite/schema/schema_utils.cc
    ${TFLM_TF}/lite/array.cc

    # 5. FullyConnected 共享代码 + hello_world 用到的 op
    ${TFLM_MICRO}/kernels/fully_connected_common.cc
    ${TFLM_MICRO}/kernels/kernel_util.cc
    ${TFLM_MICRO}/kernels/micro_tensor_utils.cc

    # 6. hello_world 模型 + 测试入口（本目录下自己写的 C++）
    ${CMAKE_CURRENT_SOURCE_DIR}/hello_world_float_model_data.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/hello_world_int8_model_data.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/tflm_hello_world.cpp
)

# 5b. FullyConnected 算子二选一
#     y: kernels/cmsis_nn/fully_connected.cc  ← MVE 优化 (需 CMSIS-NN 库)
#     n: kernels/fully_connected.cc           ← reference 实现
#     如需切换, 只需改 Kconfig; 不要手动编辑此文件.
if(CONFIG_APP_CMSIS_NN)
    list(APPEND TFLM_SOURCES ${TFLM_MICRO}/kernels/cmsis_nn/fully_connected.cc)
else()
    list(APPEND TFLM_SOURCES ${TFLM_MICRO}/kernels/fully_connected.cc)
endif()

target_sources(${APP_TARGET_NAME} PRIVATE ${TFLM_SOURCES})

# ============================================================
# 7. Include 路径
# ============================================================
target_include_directories(${APP_TARGET_NAME} PUBLIC
    ${TFLM_DIR}
    ${TFLM_TP}/flatbuffers/include
    ${TFLM_TP}/gemmlowp
    ${TFLM_TP}/ruy
    ${TFLM_TP}/kissfft
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ============================================================
# 8. 编译 flag
#
#    注意 set_source_files_properties() 只对当前 CMake 目录 scope 的 target
#    生效，而 ${APP_TARGET_NAME}=app 是在顶层 CMakeLists.txt 定义的，从
#    这里挂 property 拿不到（表现为 -std=gnu++17 丢失，array.h 里的
#    std::conditional_t / decay_t 编不过）。
#
#    因此这里用 zephyr_compile_options() 全局注入，配合
#    $<COMPILE_LANGUAGE:CXX> generator expression 只让 C++ flag
#    作用在 .cc/.cpp 上，避免每个 .c 都打
#    "warning: '-std=gnu++17' is valid for C++/ObjC++ but not for C"。
#
#    -std=gnu++17               TFLM 要求 C++17（array.h 用了 _t 别名）
#    -Wno-error                 上游有 warn-as-error 不掉的项，本地放行
#    -Wno-write-strings         string literal → char* 转换（TFLM 里较多）
# ============================================================
zephyr_compile_options(
    $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>
    -Wno-error
    -Wno-unused-variable
    -Wno-unused-function
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-write-strings>
)

# TFLM 上游 .cc 的静音项 —— 只对 TFLM 自己的源文件生效
# (这些 flag 都是 C/C++ 通用或 gcc 兼容的, source file property 够用)
set_source_files_properties(
    ${TFLM_SOURCES}
    PROPERTIES
    COMPILE_OPTIONS "-fno-exceptions;-fno-rtti;-fno-threadsafe-statics;-Wno-unused-parameter;-Wno-sign-compare;-Wno-missing-field-initializers"
)

message(STATUS "TFLite Micro: hello_world port integrated")
