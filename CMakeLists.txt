# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# ============================================================
# Location-independent anchors for the HMI application
#   HMI_APP_ROOT       -- this application's root directory (hmi repo checkout location)
#   HMI_COMPONENT_ROOT -- application component mount point: posix / FlashDB / protocol / gsensor-algorithm
#                        these west projects are mounted directly at the west topdir (repo root), alongside the application root's
#                        applications/, so this equals the parent of HMI_APP_ROOT.
#   REALTEK_APP_ROOT   -- realtek-app SDK root directory
#
# REALTEK_APP_ROOT must be available before find_package(Zephyr) (version_generate /
# post_build depend on it), so the Zephyr module system's ZEPHYR_<name>_MODULE_DIR
# variables cannot be used (those are only injected after find_package). Here we derive it from $ENV{ZEPHYR_BASE} --
# west guarantees ZEPHYR_BASE is set to zephyrproject/zephyr before invoking CMake, and
# realtek-app is fixed at zephyrproject/realtek-app as part of the SDK,
# so $ENV{ZEPHYR_BASE}/../realtek-app is a stable path.
# All references across the hmi boundary must go through these three variables; ../../ relative paths are forbidden.
# ============================================================
set(HMI_APP_ROOT       ${CMAKE_CURRENT_SOURCE_DIR})
get_filename_component(HMI_COMPONENT_ROOT ${HMI_APP_ROOT} DIRECTORY)
set(REALTEK_APP_ROOT   $ENV{ZEPHYR_BASE}/../realtek-app)

#===========version generation===========
include(${REALTEK_APP_ROOT}/cmake/version_generate.cmake)
generate_version()
#===========version generation===========
#===========post build===========
# post build should be called before find_package
include(${REALTEK_APP_ROOT}/cmake/post_build.cmake)
rtk_post_build()
#===========post build===========

# FlashDB is pulled in as a West project, and its module.yml declares kconfig-ext: True,
# so Zephyr automatically generates osource "$(ZEPHYR_FLASHDB_KCONFIG)".
# Here that variable points at an empty Kconfig inside the application project to avoid a build error.
set(ZEPHYR_FLASHDB_KCONFIG ${CMAKE_CURRENT_SOURCE_DIR}/port/flashdb/Kconfig)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(deskmate)

target_link_libraries(app PRIVATE realtek_display_driver_library)
target_link_libraries(app PRIVATE guiLib)
target_include_directories(guiLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

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

# Components moved from applications/ to the west topdir and are no longer picked up by the SUBDIRS glob below,
# so components that ship their own CMakeLists.txt are mounted explicitly here.
# FlashDB / posix need no handling here -- port/flashdb and port/posix reference their sources explicitly.
add_subdirectory(${HMI_COMPONENT_ROOT}/protocol   ${CMAKE_CURRENT_BINARY_DIR}/component/protocol)
set(GSENSOR_ALGORITHM_ROOT ${HMI_COMPONENT_ROOT}/gsensor-algorithm)
file(GLOB GSENSOR_ALGORITHM_SOURCES ${GSENSOR_ALGORITHM_ROOT}/src/*.c)
if(NOT GSENSOR_ALGORITHM_SOURCES)
    message(FATAL_ERROR "gsensor-algorithm: no sources found under ${GSENSOR_ALGORITHM_ROOT}/src")
endif()
target_sources(${APP_TARGET_NAME} PRIVATE ${GSENSOR_ALGORITHM_SOURCES})
target_include_directories(${APP_TARGET_NAME} PUBLIC
    ${GSENSOR_ALGORITHM_ROOT}/include
    ${GSENSOR_ALGORITHM_ROOT}/src)

# Optional embedded pedometer replay trace. The source is harmless when
# HEALTH_USE_SD001_SAMPLE is 0 because the linker drops the unreferenced data.
set(GSENSOR_SD001_SAMPLE_DIR
    ${GSENSOR_ALGORITHM_ROOT}/sample/pedometer/25hz)
if(EXISTS ${GSENSOR_SD001_SAMPLE_DIR}/sample_25hz_sd001.c)
    target_sources(${APP_TARGET_NAME} PRIVATE
        ${GSENSOR_SD001_SAMPLE_DIR}/sample_25hz_sd001.c)
    target_include_directories(${APP_TARGET_NAME} PUBLIC
        ${GSENSOR_SD001_SAMPLE_DIR})
endif()

# Product UI exported by HoneyGUI Designer (west project watch_ui, also mounted at the topdir).
# Generated artifacts are not copied into the firmware tree; ui/ is used directly as the source dir to avoid hundreds of generated copies.
add_subdirectory(${HMI_COMPONENT_ROOT}/ui ${CMAKE_CURRENT_BINARY_DIR}/watch_ui)

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
