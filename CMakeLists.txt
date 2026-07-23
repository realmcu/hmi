# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# ============================================================
# HMI 应用位置无关锚点
#   REALTEK_APP_ROOT — realtek-app SDK 根目录
#
# REALTEK_APP_ROOT 必须在 find_package(Zephyr) 之前可用（version_generate /
# post_build 依赖它），因此不能用 Zephyr module 系统的 ZEPHYR_<name>_MODULE_DIR
# 变量（那些要 find_package 之后才注入）。此处用 $ENV{ZEPHYR_BASE} 派生 ——
# west 在调用 CMake 前保证 ZEPHYR_BASE 已设置为 zephyrproject/zephyr，
# realtek-app 作为 SDK 一部分固定在 zephyrproject/realtek-app，
# 因此 $ENV{ZEPHYR_BASE}/../realtek-app 是稳定路径，与 hmi 检出深浅无关。
# 禁止用 ../../ 相对路径跨 hmi 边界引用。
# ============================================================
set(REALTEK_APP_ROOT $ENV{ZEPHYR_BASE}/../realtek-app)

#===========version generation===========
include(${REALTEK_APP_ROOT}/cmake/version_generate.cmake)
generate_version()
#===========version generation===========
#===========post build===========
# post build should be called before find_package
include(${REALTEK_APP_ROOT}/cmake/post_build.cmake)
rtk_post_build()
#===========post build===========

# FlashDB 以 West project 方式引入，其 module.yml 声明了 kconfig-ext: True，
# Zephyr 会自动生成 osource "$(ZEPHYR_FLASHDB_KCONFIG)"。
# 此处将该变量指向应用工程内的空 Kconfig，避免构建报错。
set(ZEPHYR_FLASHDB_KCONFIG ${CMAKE_CURRENT_SOURCE_DIR}/port/flashdb/Kconfig)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(deskmate)

target_link_libraries(app PRIVATE realtek_display_driver_library)
target_link_libraries(app PRIVATE guiLib)

zephyr_compile_options(
    -Wno-unused-variable
    -Wno-maybe-uninitialized
    -Wno-double-promotion
    -Wno-stringop-truncation
    -Wno-unused-function
    -Wno-missing-braces
    -Wno-use-after-free
    -Wno-parentheses
    -Wno-address
    -Wno-format-overflow
    -Wno-overflow
    -Wno-char-subscripts
    -Wno-enum-compare
)

set(APP_TARGET_NAME app)

if(1) #set to 1 to enable or variable to for depnedency

    file(GLOB SOURCES "*.c" "*.cpp")
    file(GLOB HEADERS "*.h" "*.hpp")


    # Determine if we should create a library
    if((NOT HEADERS) AND (NOT SOURCES))
        message(STATUS "No source or header files found in ${CMAKE_CURRENT_SOURCE_DIR}.")
		target_include_directories(${APP_TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    endif()

    if(HEADERS AND (NOT SOURCES))
        target_include_directories(${APP_TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    endif()

    if(SOURCES AND (NOT HEADERS))
        target_sources(${APP_TARGET_NAME} PRIVATE ${SOURCES})
    endif()

    if(SOURCES AND HEADERS)
        target_sources(${APP_TARGET_NAME} PRIVATE ${SOURCES})
        target_include_directories(${APP_TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    endif()

    # Process subdirectories regardless
    file(GLOB SUBDIRS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/*)

    foreach(subdir ${SUBDIRS})
        if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${subdir})
            if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${subdir}/CMakeLists.txt)
                add_subdirectory(${subdir})
            else()
                message(STATUS "Skipping directory ${subdir} as it does not contain a CMakeLists.txt file.")
            endif()
        endif()
    endforeach()
endif()
