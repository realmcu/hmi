# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

#===========version generation===========
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/version_generate.cmake)
generate_version()
#===========version generation===========
#===========post build===========
# post build should be called before find_package
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/post_build.cmake)
rtk_post_build()
#===========post build===========

# FlashDB is pulled in as a West project and its module.yml declares kconfig-ext: True,
# so Zephyr emits osource "$(ZEPHYR_FLASHDB_KCONFIG)" automatically.
# Point that variable at the empty Kconfig kept in this application to keep the build happy.
set(ZEPHYR_FLASHDB_KCONFIG ${CMAKE_CURRENT_SOURCE_DIR}/port/flashdb/Kconfig)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(dashboard)

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

    # ui_design/ is wired up explicitly at the end of this file: it is a separate git
    # repository exported by HoneyGUI Designer and must stay free of build files.
    list(REMOVE_ITEM SUBDIRS ui_design)

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

# ---------------------------------------------------------------------------
# HoneyGUI Visual Designer UI (ui_design/)
#
# Handled here instead of a ui_design/CMakeLists.txt, because ui_design/ is its
# own git repository exported by the designer: keeping the build wiring in the
# application avoids leaving an untracked file inside that repository.
#
# The designer emits the UI as C code under ui_design/src/:
#   src/dashboardEntry.c          - GUI_INIT_APP_EXPORT entry, mounts romfs + creates the root view
#   src/ui/DashboardMain_ui.c     - view / widget tree
#   src/callbacks/                - widget callbacks
#   src/user/                     - application logic (data refresh, shell commands, ...)
#
# Assets (images/fonts) are read at runtime from the romfs in flash. The address
# comes from romfsBaseAddr in ui_design/project.json and must match
# USER_DATA1_ADDR in flash_map.h. The content (ui_design/build/app_romfs.bin) is
# flashed to that address and is not part of the compile step.
# ---------------------------------------------------------------------------
set(UI_DESIGN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ui_design)
file(GLOB_RECURSE UI_SOURCES "${UI_DESIGN_DIR}/src/*.c" "${UI_DESIGN_DIR}/src/*.cpp")

if(NOT UI_SOURCES)
    message(WARNING "ui_design: no generated sources under ${UI_DESIGN_DIR}/src, UI will not be built")
else()
    target_sources(${APP_TARGET_NAME} PRIVATE ${UI_SOURCES})

    # Generated code includes its siblings through paths such as ../callbacks/ and
    # ../user/, so every level is exported as an include directory to keep the
    # build layout identical to the designer project.
    target_include_directories(${APP_TARGET_NAME} PUBLIC
        ${UI_DESIGN_DIR}/src
        ${UI_DESIGN_DIR}/src/callbacks
        ${UI_DESIGN_DIR}/src/ui
        ${UI_DESIGN_DIR}/src/user
    )
    list(LENGTH UI_SOURCES UI_SOURCE_COUNT)
    message(STATUS "ui_design: ${UI_SOURCE_COUNT} generated source(s) added to ${APP_TARGET_NAME}")
endif()
