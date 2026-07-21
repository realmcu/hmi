# Copyright (c) 2024 Realtek Semiconductor Corp.
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.10)


# Define a variable for the library name
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
        target_include_directories(${APP_TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../key_button)
        target_include_directories(${APP_TARGET_NAME} PUBLIC ${ZEPHYR_BASE}/../modules/display/device/key_button/8773G)
    endif()

    # Process subdirectories regardless
    file(GLOB SUBDIRS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/*)

    # 跳过列表: 这些 vendored 库自带官方 CMakeLists.txt(用于它们独立 build),
    # 我们不用官方 CMake 目标, 而是在对应的 port/<name>/ 里用 target_sources
    # 精确挑源码编进 app. 若这里 add_subdirectory 上去, 会额外产生一个孤儿
    # static library (例如 libcmsis-nn.a), 浪费编译时间且可能带来符号/flag 冲突.
    set(COMPONENT_SUBDIR_SKIP_LIST
        cmsis-nn
    )

    foreach(subdir ${SUBDIRS})
        if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${subdir})
            if(${subdir} IN_LIST COMPONENT_SUBDIR_SKIP_LIST)
                message(STATUS "Skipping vendored dir '${subdir}' (handled by port/${subdir}/)")
                continue()
            endif()
            if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${subdir}/CMakeLists.txt)
                add_subdirectory(${subdir})
            else()
                message(STATUS "Skipping directory ${subdir} as it does not contain a CMakeLists.txt file.")
            endif()
        endif()
    endforeach()
endif()
