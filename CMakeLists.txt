# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# ============================================================
# HMI application location-independent anchor
#   REALTEK_APP_ROOT - realtek-app SDK root directory
#
# REALTEK_APP_ROOT must be available before find_package(Zephyr) (version_generate /
# post_build depend on it), so the Zephyr module system variables ZEPHYR_<name>_MODULE_DIR
# cannot be used (they are injected only after find_package). Derive it from $ENV{ZEPHYR_BASE} --
# west guarantees ZEPHYR_BASE is set to zephyrproject/zephyr before it invokes CMake,
# and realtek-app is fixed at zephyrproject/realtek-app as part of the SDK,
# so $ENV{ZEPHYR_BASE}/../realtek-app is a stable path regardless of hmi checkout depth.
# Do not reference across the hmi boundary with ../../ relative paths.
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

# FlashDB is pulled in as a West project; its module.yml declares kconfig-ext: True,
# so Zephyr generates osource "$(ZEPHYR_FLASHDB_KCONFIG)" automatically.
# Point that variable at the empty Kconfig in this application to avoid a build error.
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
