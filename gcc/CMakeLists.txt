#
# Copyright (c) 2026, Realtek Semiconductor Corporation
#
# SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
#
message(STATUS "[APP]hmi_dashboard cmake start build")

#==============================================================================
# Global Include Paths (for submodules: display driver needs os_mem.h etc.)
# TODO: Remove when display library properly propagates SDK include paths via target
#==============================================================================
include_directories(${REALTEK_SDK_BASE_PATH}/inc/os)

# Preinclude file (equivalent to MDK pre_define.h)
# This defines RAM_TYPE_DATA_ON and other macros needed by display drivers
set(PRE_DEFINE_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../mdk/pre_define.h)

# Add preinclude globally so display library also gets it
add_compile_options(-include ${PRE_DEFINE_FILE})


#==============================================================================
# Display Driver Library Build
#==============================================================================
if(CONFIG_REALTEK_DISPLAY)
    add_subdirectory(${REALTEK_SDK_BASE_PATH}/src/mcu/display ${CMAKE_CURRENT_BINARY_DIR}/display)

    # PUBLIC: propagates to honeygui via link, so no need to re-define there.
    target_compile_definitions(realtek_display_driver_library PUBLIC
        TARGET_RTL8773E=1
        CONFIG_SOC_SERIES_RTL8773E=1
    )

    # Provide dashboard project header paths (mem_config.h, etc.)
    # consumed by display device drivers (e.g. lcd_st7265_800480_rgb.c)
    target_include_directories(realtek_display_driver_library PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../inc/rtl87x3ep
    )
endif()

#==============================================================================
# GUI Library Build (source or precompiled)
#==============================================================================
if(CONFIG_REALTEK_HONEYGUI)
    add_subdirectory(${REALTEK_SDK_BASE_PATH}/src/sample/gui ${CMAKE_CURRENT_BINARY_DIR}/gui)
endif()

set(CMAKE_EXECUTABLE_SUFFIX ".elf")
enable_language(ASM)
enable_language(CXX)

add_executable(honeygui)
set(build_target_list ${build_target_list} "honeygui" CACHE INTERNAL "build target list")
set(BIN_OUTPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/bin/${bin_output_path})

if(EXISTS ${BIN_OUTPUT_DIR})
    file(GLOB OUT_FILES ${BIN_OUTPUT_DIR}/*MP*.bin)
    if(NOT "${OUT_FILES}" STREQUAL "")
        file(REMOVE ${OUT_FILES})
    endif()
endif()

set_target_properties(honeygui PROPERTIES
                      ARCHIVE_OUTPUT_DIRECTORY ${lib_output_path}
                      LIBRARY_OUTPUT_DIRECTORY ${BIN_OUTPUT_DIR}
                      RUNTIME_OUTPUT_DIRECTORY ${BIN_OUTPUT_DIR}
                      OUTPUT_NAME honeygui_${build_bank})

#==============================================================================
# GUI Library Configuration
#==============================================================================
if(CONFIG_REALTEK_HONEYGUI)
    message(STATUS "[HoneyGUI] Building from source code")

    target_link_libraries(honeygui PUBLIC
        -Wl,--whole-archive guiLib -Wl,--no-whole-archive
        ${lib_gap}
        ${lib_rom}
        ${lib_upperstack}
        halLib
        filesystemLib
        sysmLib
        romLib
        upperstackLib
        )

    # Link display driver library if enabled
    if(CONFIG_REALTEK_DISPLAY)
        target_link_libraries(honeygui PUBLIC realtek_display_driver_library)
    endif()

    target_compile_definitions(honeygui PRIVATE
        CONFIG_REALTEK_HONEYGUI=1
        CONFIG_REALTEK_BUILD_HONEYGUI_SRC=1
        ENABLE_HONEYGUI
    )

else()
    # Use precompiled library
    message(STATUS "[HoneyGUI] Using precompiled library")

    target_include_directories(honeygui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/gui_lib/include)
    target_link_libraries(honeygui PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/gui_lib/gcc/libgui.a
        ${lib_gap}
        ${lib_rom}
        ${lib_upperstack}
        halLib
        filesystemLib
        sysmLib
        romLib
        upperstackLib
        )
endif()

#==============================================================================
# Application Source Files
#==============================================================================
file(GLOB APP_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../src/application/*.c)
if(CONFIG_REALTEK_HONEYGUI)
    list(FILTER APP_SOURCES EXCLUDE REGEX "example_gui_img\\.c$")
endif()
target_sources(honeygui PRIVATE ${APP_SOURCES})
target_include_directories(honeygui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/application)

file(GLOB PORT_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../src/ports/realgui_port/*.c)
target_sources(honeygui PRIVATE ${PORT_SOURCES})
target_include_directories(honeygui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/ports/realgui_port)

set(DEVICE_SRC_DIR ${REALTEK_SDK_BASE_PATH}/src/app/Wearable)

target_sources(honeygui PRIVATE ${DEVICE_SRC_DIR}/driver/io/drv_lcd.c)
target_include_directories(honeygui PRIVATE ${DEVICE_SRC_DIR}/driver/io)

# ROMFS
if(CONFIG_REALTEK_ROMFS)
    target_sources(honeygui PRIVATE ${DEVICE_SRC_DIR}/system/dfs/romfs/romfs.c)
    target_include_directories(honeygui PRIVATE ${DEVICE_SRC_DIR}/system/dfs/romfs)
endif()

#==============================================================================
# Library Link (when compile_lib is set)
#==============================================================================
if(compile_lib)
    target_link_libraries(honeygui PUBLIC
        ${halLib_link}
        ${filesystemLib_link}
        ${sysmLib_link}
        )
endif()

#==============================================================================
# Compile Definitions
#==============================================================================
target_compile_definitions(honeygui PRIVATE
    SUPPORT_ACCESS_SHM=1
    RAM_TYPE_DATA_ON=0
)

#==============================================================================
# Include Directories
#==============================================================================
target_precompile_headers(honeygui PRIVATE ${kconfig_header_dir_path}/config.h)
target_include_directories(honeygui PRIVATE
    ${kconfig_header_dir_path}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../inc/rtl87x3ep
    ${CMAKE_CURRENT_SOURCE_DIR}/../mdk
    ${sdk_app_dir_path}
)

#==============================================================================
# Boot Files
#==============================================================================
target_sources(honeygui PRIVATE
    ${realtek_start_up_boot_file}
    ${arm_start_up_boot_file})
set_property(SOURCE ${arm_start_up_boot_file} PROPERTY LANGUAGE C)

#==============================================================================
# Version Generation
#==============================================================================
set(version_path ${CMAKE_CURRENT_SOURCE_DIR}/../version.h)
generate_version("honeygui_app" ${version_path})

#==============================================================================
# Link Script
#==============================================================================
generate_ldscript("${CMAKE_CURRENT_SOURCE_DIR}/app.ld" honeygui)

#==============================================================================
# Compile Options
#==============================================================================
target_compile_options(honeygui PRIVATE
                    ${mcpu_arch}
                    ${mfpu_arch}
                    ${mfloat_arch}
                    ${march_arch}
                    -flto
                    -mthumb
                    -Wall
                    -Wno-parentheses
                    -Wno-unused-variable
                    -fdata-sections
                    -ffunction-sections
                    -std=gnu99)

#==============================================================================
# Link Options
#==============================================================================
target_link_options(honeygui
                    PRIVATE
                    ${mcpu_arch}
                    ${mfpu_arch}
                    ${mfloat_arch}
                    ${march_arch}
                    -mthumb
                    -Wall
                    -Wl,-Map=${BIN_OUTPUT_DIR}/honeygui_${build_bank}.map
                    -fdata-sections
                    -ffunction-sections
                    -Wl,--gc-sections
                    -Wl,--cref
                    -specs=nosys.specs
                    -Wl,-wrap=malloc
                    -Wl,-wrap=calloc
                    -Wl,-wrap=free
                    -Wl,-wrap=memset_s
                    -Wl,-wrap=memcpy_s)

#==============================================================================
# Post Build Commands
#==============================================================================
set(gcc_tool_path ${REALTEK_SDK_BASE_PATH}/tool/Gadgets/gcc_tool)
set(debug_table_tool_path ${REALTEK_SDK_BASE_PATH}/tool/Gadgets/build_debug_table_tool)

# Platform-specific tool selection
set(gcc_tool_linux_path ${REALTEK_SDK_BASE_PATH}/tool/Gadgets/gcc_tool_linux)
if(BUILD_IN_WINDOW)
    set(HEX2BIN_TOOL           ${gcc_tool_path}/Hex2Bin.exe)
    set(PREPEND_HEADER_TOOL    ${gcc_tool_path}/prepend_header.exe)
    set(BUILD_DEBUG_TABLE_TOOL ${debug_table_tool_path}/build_debug_table.exe)
else()
    set(HEX2BIN_TOOL           ${gcc_tool_linux_path}/Hex2Bin)
    set(PREPEND_HEADER_TOOL    ${gcc_tool_linux_path}/prepend_header)
    set(BUILD_DEBUG_TABLE_TOOL ${gcc_tool_linux_path}/build_debug_table)
endif()

add_custom_command(TARGET honeygui POST_BUILD
            WORKING_DIRECTORY ${BIN_OUTPUT_DIR}
            COMMAND ${CMAKE_OBJCOPY} -O ihex ${BIN_OUTPUT_DIR}/honeygui_${build_bank}.elf ${BIN_OUTPUT_DIR}/honeygui_${build_bank}.hex
            COMMAND ${HEX2BIN_TOOL} ${BIN_OUTPUT_DIR}/honeygui_${build_bank}.hex ${BIN_OUTPUT_DIR}/honeygui_${build_bank}.bin
            COMMAND ${PREPEND_HEADER_TOOL} /app_code ${BIN_OUTPUT_DIR}/honeygui_${build_bank}.bin /${encryption_type} ${gcc_tool_path}/../${encryption_key}
            COMMAND ${PREPEND_HEADER_TOOL} /app_code ${BIN_OUTPUT_DIR}/honeygui_${build_bank}.bin /mp_ini ${CMAKE_CURRENT_SOURCE_DIR}/../mdk/mp.ini
            COMMAND bash ${gcc_tool_path}/md5_generate.sh ${BIN_OUTPUT_DIR}/honeygui_${build_bank}_MP.bin ${version_path}
            )

if(NOT ((DEFINED IS_CI_CHECK_FLOW) AND IS_CI_CHECK_FLOW))
    add_custom_command(TARGET honeygui POST_BUILD
            WORKING_DIRECTORY ${BIN_OUTPUT_DIR}
            COMMAND ${BUILD_DEBUG_TABLE_TOOL} --debug_dir ${BIN_OUTPUT_DIR} --su_dir ${CMAKE_CURRENT_SOURCE_DIR}/../../../../build --out_dir ${BIN_OUTPUT_DIR}
            COMMAND ${CMAKE_OBJDUMP} -D -S ${BIN_OUTPUT_DIR}/honeygui_${build_bank}.elf > ${BIN_OUTPUT_DIR}/honeygui_${build_bank}.disasm
            )
    message(STATUS "Debug files are being generated.")
endif()
