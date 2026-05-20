#
# Copyright (c) 2026, Realtek Semiconductor Corporation
#
# SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
#
message(STATUS "[APP]hmi_dashboard cmake start build")

#==============================================================================
# Global Include Paths (for submodules that need app config headers)
#==============================================================================
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${REALTEK_SDK_BASE_PATH}/inc/os
)

# Preinclude file (equivalent to MDK pre_define.h)
# This defines RAM_TYPE_DATA_ON and other macros needed by display drivers
set(PRE_DEFINE_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../mdk/pre_define.h)

# Add preinclude globally so display library also gets it
add_compile_options(-include ${PRE_DEFINE_FILE})


#==============================================================================
# Display Driver Library Build
#==============================================================================
if(CONFIG_REALTEK_DISPLAY)
    # Build display driver library from source
    # This creates the realtek_display_driver_library target
    add_subdirectory(${REALTEK_SDK_BASE_PATH}/src/mcu/display ${CMAKE_CURRENT_BINARY_DIR}/display)

    # Propagate chip macros to the display library so platform-specific
    # code (e.g. LCDC_Clock_Cfg #elif branches) compiles correctly.
    target_compile_definitions(realtek_display_driver_library PRIVATE
        TARGET_RTL8773E=1
        CONFIG_SOC_SERIES_RTL8773E=1
    )
endif()

#==============================================================================
# GUI Library Build (source or precompiled)
#==============================================================================
if(CONFIG_REALTEK_HONEYGUI)
    # Build GUI from source code (GUI sub-repository)
    # This creates the guiLib target that we link against below
    add_subdirectory(${REALTEK_SDK_BASE_PATH}/src/sample/gui ${CMAKE_CURRENT_BINARY_DIR}/gui)
endif()

set(CMAKE_EXECUTABLE_SUFFIX ".elf")
enable_language(ASM)
enable_language(CXX)

add_executable(honeygui)
set(build_target_list ${build_target_list} "honeygui" CACHE INTERNAL "build target list")

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/bin/${bin_output_path})
file(GLOB OUT_FILES ${CMAKE_CURRENT_SOURCE_DIR}/bin/${bin_output_path}/*MP*.bin)
if(NOT "${OUT_FILES}" STREQUAL "")
file(REMOVE ${OUT_FILES})
endif()
endif()

set_target_properties(honeygui PROPERTIES
                      ARCHIVE_OUTPUT_DIRECTORY ${lib_output_path}
                      LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin/${bin_output_path}
                      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin/${bin_output_path}
                      OUTPUT_NAME honeygui_${build_bank})

#==============================================================================
# GUI Library Configuration
#==============================================================================
if(CONFIG_REALTEK_HONEYGUI)
    # Build GUI from source code (GUI sub-repository)
    message(STATUS "[HoneyGUI] Building from source code")

    target_link_libraries(honeygui
        PUBLIC -Wl,--whole-archive guiLib -Wl,--no-whole-archive
        PUBLIC ${lib_gap}
        PUBLIC ${lib_rom}
        PUBLIC ${lib_upperstack}
        PUBLIC halLib
        PUBLIC filesystemLib
        PUBLIC sysmLib
        PUBLIC romLib
        PUBLIC upperstackLib
        )

    # Link display driver library if enabled
    if(CONFIG_REALTEK_DISPLAY)
        target_link_libraries(honeygui PUBLIC realtek_display_driver_library)
    endif()

    # HoneyGUI compile definitions
    target_compile_definitions(honeygui PRIVATE
        CONFIG_REALTEK_HONEYGUI=1
        CONFIG_REALTEK_BUILD_HONEYGUI_SRC=1
        ENABLE_HONEYGUI
    )

    # Note: Demo selection and feature definitions are now handled internally
    # by honeygui submodule (sdk/src/sample/gui/CMakeLists.txt).
    # guiLib target will have the appropriate definitions applied automatically.

else()
    # Use precompiled library
    message(STATUS "[HoneyGUI] Using precompiled library")

    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../src/gui_lib/include)
    target_link_libraries(honeygui
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../src/gui_lib/gcc/libgui.a
        PUBLIC ${lib_gap}
        PUBLIC ${lib_rom}
        PUBLIC ${lib_upperstack}
        PUBLIC halLib
        PUBLIC filesystemLib
        PUBLIC sysmLib
        PUBLIC romLib
        PUBLIC upperstackLib
        )
endif()

#==============================================================================
# Application Source Files
#==============================================================================
# Application layer sources
file(GLOB APP_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../src/application/*.c)
if(CONFIG_REALTEK_HONEYGUI)
    list(FILTER APP_SOURCES EXCLUDE REGEX "example_gui_img\\.c$")
endif()
target_sources(honeygui PRIVATE ${APP_SOURCES})
target_include_directories(honeygui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/application)

# Port layer sources
file(GLOB PORT_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../src/ports/realgui_port/*.c)
target_sources(honeygui PRIVATE ${PORT_SOURCES})
target_include_directories(honeygui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/ports/realgui_port)

# Note: Display hardware drivers (LCDC, IDU, PPE) and LCD device drivers are now
# provided by realtek_display_driver_library target via CONFIG_REALTEK_DISPLAY

# Device driver sources (from Wearable) - these are application-specific drivers
set(DEVICE_SRC_DIR ${REALTEK_SDK_BASE_PATH}/src/app/Wearable)

# LCD driver (Wearable-specific LCD abstraction layer)
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
    target_link_libraries(honeygui
        PUBLIC ${halLib_link}
        PUBLIC ${filesystemLib_link}
        PUBLIC ${sysmLib_link}
        )
endif()

#==============================================================================
# Include Directories
#==============================================================================
# Preinclude kconfig.h
target_precompile_headers(honeygui PRIVATE ${kconfig_header_dir_path}/config.h)
target_include_directories(honeygui PRIVATE ${kconfig_header_dir_path})

# Application compile definitions
target_compile_definitions(honeygui PRIVATE CONFIG_REALTEK_APP_HONEYGUI_DEMO_SUPPORT=1)
target_compile_definitions(honeygui PRIVATE TARGET_RTL8773E=1
                                            CONFIG_SOC_SERIES_RTL8773E=1
                                            SUPPORT_ACCESS_SHM=1
                                            RTK_MODULE_RTK_PPEV2
                                            )

# Include directory for board.h config.h
target_include_directories(honeygui PUBLIC ../)
target_include_directories(honeygui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(honeygui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../inc/rtl87x3ep)
target_include_directories(honeygui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../mdk)

# SDK inc/app
target_include_directories(honeygui PRIVATE ${sdk_app_dir_path})

# Additional compile definitions
target_compile_definitions(honeygui PUBLIC RAM_TYPE_DATA_ON=0)

#==============================================================================
# Boot Files
#==============================================================================
target_sources(honeygui
               PRIVATE ${realtek_start_up_boot_file}
               PRIVATE ${arm_start_up_boot_file})
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
                    -std=gnu99
                    -include ${PRE_DEFINE_FILE})

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
                    -Wl,-Map=${CMAKE_CURRENT_SOURCE_DIR}/bin/${bin_output_path}/honeygui_${build_bank}.map
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
    set(HEX2BIN_TOOL ${gcc_tool_path}/Hex2Bin.exe)
    set(PREPEND_HEADER_TOOL ${gcc_tool_path}/prepend_header.exe)
else()
    set(HEX2BIN_TOOL ${gcc_tool_linux_path}/Hex2Bin)
    set(PREPEND_HEADER_TOOL ${gcc_tool_linux_path}/prepend_header)
endif()

add_custom_command(TARGET honeygui POST_BUILD
            WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/bin/${bin_output_path}
            COMMAND ${CMAKE_OBJCOPY} -O ihex ${CMAKE_CURRENT_SOURCE_DIR}/bin/${bin_output_path}/honeygui_${build_bank}.elf ${CMAKE_CURRENT_SOURCE_DIR}/bin/${bin_output_path}/honeygui_${build_bank}.hex
            COMMAND ${HEX2BIN_TOOL} ${CMAKE_CURRENT_SOURCE_DIR}/bin/${bin_output_path}/honeygui_${build_bank}.hex ${CMAKE_CURRENT_SOURCE_DIR}/bin/${bin_output_path}/honeygui_${build_bank}.bin
            COMMAND ${PREPEND_HEADER_TOOL} /app_code ${CMAKE_CURRENT_SOURCE_DIR}/bin/${bin_output_path}/honeygui_${build_bank}.bin /${encryption_type} ${gcc_tool_path}/../${encryption_key}
            COMMAND ${PREPEND_HEADER_TOOL} /app_code ${CMAKE_CURRENT_SOURCE_DIR}/bin/${bin_output_path}/honeygui_${build_bank}.bin /mp_ini ${CMAKE_CURRENT_SOURCE_DIR}/../mdk/mp.ini
            COMMAND bash ${gcc_tool_path}/md5_generate.sh ${CMAKE_CURRENT_SOURCE_DIR}/bin/${bin_output_path}/honeygui_${build_bank}_MP.bin ${version_path}
            )

if(BUILD_IN_WINDOW)
    set(BUILD_DEBUG_TABLE_TOOL ${debug_table_tool_path}/build_debug_table.exe)
else()
    set(BUILD_DEBUG_TABLE_TOOL ${gcc_tool_linux_path}/build_debug_table)
endif()

if(NOT ((DEFINED IS_CI_CHECK_FLOW) AND IS_CI_CHECK_FLOW))
    add_custom_command(TARGET honeygui POST_BUILD
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin/${bin_output_path}
            COMMAND ${BUILD_DEBUG_TABLE_TOOL} --debug_dir ${CMAKE_CURRENT_SOURCE_DIR}/bin/${bin_output_path} --su_dir ${CMAKE_CURRENT_SOURCE_DIR}/../../../../build --out_dir ${CMAKE_CURRENT_SOURCE_DIR}/bin/${bin_output_path}
            COMMAND ${CMAKE_OBJDUMP} -D -S ${CMAKE_CURRENT_SOURCE_DIR}/bin/${bin_output_path}/honeygui_${build_bank}.elf > ${CMAKE_CURRENT_SOURCE_DIR}/bin/${bin_output_path}/honeygui_${build_bank}.disasm
            )
    message(STATUS "Debug files are being generated.")
endif()
