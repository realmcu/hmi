# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# 查找 Zephyr 包
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

# 项目名称
project(rtl8773g_hmi)

# 添加源文件
target_sources(app PRIVATE
    src/main.c
)

# 包含头文件目录
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
