#
# Copyright (c) 2026, Realtek Semiconductor Corporation
#
# SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
#
message([STATUS] "[APP]hmi_dashboard cmake start build")

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

add_executable(honeygui " ")
set(build_target_list ${build_target_list} "honeygui" CACHE INTERNAL "build target list")

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/bin/${bin_output_path})
file(GLOB OUT_FILES ${CMAKE_CURRENT_SOURCE_DIR}/bin/${bin_output_path}/*MP*.bin)
if(NOT "${OUT_FILES}" STREQUAL "")
file(REMOVE ${OUT_FILES})
endif()
endif()

set_target_properties(honeygui PROPERTIES
                      ARCHIVE_OUTPUT_DIRECTORY ${lib_output_path}
                      LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
                      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin/${bin_output_path}
                      OUTPUT_NAME honeygui_${build_bank})

#==============================================================================
# GUI Library Configuration
#==============================================================================
if(CONFIG_REALTEK_HONEYGUI)
    # Build GUI from source code (GUI sub-repository)
    message(STATUS "[HoneyGUI] Building from source code")

    target_link_libraries(honeygui
        PUBLIC guiLib
        PUBLIC ${lib_gap}
        PUBLIC ${lib_rom}
        PUBLIC ${lib_upperstack}
        PUBLIC halLib
        PUBLIC filesystemLib
        PUBLIC sysmLib
        PUBLIC romLib
        PUBLIC upperstackLib
        )

    # HoneyGUI compile definitions
    target_compile_definitions(honeygui PRIVATE
        CONFIG_REALTEK_HONEYGUI=1
        CONFIG_REALTEK_BUILD_HONEYGUI_SRC=1
        ENABLE_HONEYGUI
    )

    # Demo selection definitions (from Kconfig)
    if(CONFIG_REALTEK_BUILD_EXAMPLE_IMAGE_WIDGET)
        target_compile_definitions(honeygui PRIVATE CONFIG_REALTEK_BUILD_EXAMPLE_IMAGE_WIDGET=1)
    elseif(CONFIG_REALTEK_BUILD_EXAMPLE_SVG_WIDGET)
        target_compile_definitions(honeygui PRIVATE CONFIG_REALTEK_BUILD_EXAMPLE_SVG_WIDGET=1)
    elseif(CONFIG_REALTEK_BUILD_EXAMPLE_GIF_WIDGET)
        target_compile_definitions(honeygui PRIVATE CONFIG_REALTEK_BUILD_EXAMPLE_GIF_WIDGET=1)
    elseif(CONFIG_REALTEK_BUILD_EXAMPLE_TEXT_WIDGET)
        target_compile_definitions(honeygui PRIVATE CONFIG_REALTEK_BUILD_EXAMPLE_TEXT_WIDGET=1)
    elseif(CONFIG_REALTEK_BUILD_EXAMPLE_GRAY_WIDGET)
        target_compile_definitions(honeygui PRIVATE CONFIG_REALTEK_BUILD_EXAMPLE_GRAY_WIDGET=1)
    elseif(CONFIG_REALTEK_BUILD_REAL_DOG_3D)
        target_compile_definitions(honeygui PRIVATE CONFIG_REALTEK_BUILD_REAL_DOG_3D=1)
    elseif(CONFIG_REALTEK_BUILD_REAL_EARTH_3D)
        target_compile_definitions(honeygui PRIVATE CONFIG_REALTEK_BUILD_REAL_EARTH_3D=1)
    elseif(CONFIG_REALTEK_BUILD_REAL_DONUT_3D)
        target_compile_definitions(honeygui PRIVATE CONFIG_REALTEK_BUILD_REAL_DONUT_3D=1)
    elseif(CONFIG_REALTEK_BUILD_REAL_PIKACHU_GLTF_3D)
        target_compile_definitions(honeygui PRIVATE CONFIG_REALTEK_BUILD_REAL_PIKACHU_GLTF_3D=1)
    elseif(CONFIG_REALTEK_BUILD_REAL_FLAG_GLTF_3D)
        target_compile_definitions(honeygui PRIVATE CONFIG_REALTEK_BUILD_REAL_FLAG_GLTF_3D=1)
    elseif(CONFIG_REALTEK_BUILD_REAL_ROBOT_GLTF_3D)
        target_compile_definitions(honeygui PRIVATE CONFIG_REALTEK_BUILD_REAL_ROBOT_GLTF_3D=1)
    elseif(CONFIG_REALTEK_BUILD_REAL_VIEW)
        target_compile_definitions(honeygui PRIVATE CONFIG_REALTEK_BUILD_REAL_VIEW=1)
    elseif(CONFIG_REALTEK_BUILD_EXAMPLE_LIST_WIDGET)
        target_compile_definitions(honeygui PRIVATE CONFIG_REALTEK_BUILD_EXAMPLE_LIST_WIDGET=1)
    elseif(CONFIG_REALTEK_BUILD_EXAMPLE_MENU_CELLULAR_WIDGET)
        target_compile_definitions(honeygui PRIVATE CONFIG_REALTEK_BUILD_EXAMPLE_MENU_CELLULAR_WIDGET=1)
    elseif(CONFIG_REALTEK_BUILD_REAL_CANVAS)
        target_compile_definitions(honeygui PRIVATE CONFIG_REALTEK_BUILD_REAL_CANVAS=1)
    elseif(CONFIG_REALTEK_BUILD_REAL_LITE_ARC)
        target_compile_definitions(honeygui PRIVATE CONFIG_REALTEK_BUILD_REAL_LITE_ARC=1)
    elseif(CONFIG_REALTEK_BUILD_REAL_LITE_RECT)
        target_compile_definitions(honeygui PRIVATE CONFIG_REALTEK_BUILD_REAL_LITE_RECT=1)
    elseif(CONFIG_REALTEK_BUILD_REAL_LITE_CIRCLE)
        target_compile_definitions(honeygui PRIVATE CONFIG_REALTEK_BUILD_REAL_LITE_CIRCLE=1)
    elseif(CONFIG_REALTEK_BUILD_EXAMPLE_QBCODE_WIDGET)
        target_compile_definitions(honeygui PRIVATE CONFIG_REALTEK_BUILD_EXAMPLE_QBCODE_WIDGET=1)
    elseif(CONFIG_REALTEK_BUILD_EXAMPLE_GLASS_EFFECT_WIDGET)
        target_compile_definitions(honeygui PRIVATE CONFIG_REALTEK_BUILD_EXAMPLE_GLASS_EFFECT_WIDGET=1)
    elseif(CONFIG_REALTEK_BUILD_EXAMPLE_VIDEO_WIDGET)
        target_compile_definitions(honeygui PRIVATE CONFIG_REALTEK_BUILD_EXAMPLE_VIDEO_WIDGET=1)
    elseif(CONFIG_REALTEK_BUILD_REAL_PAINTENGINE)
        target_compile_definitions(honeygui PRIVATE CONFIG_REALTEK_BUILD_REAL_PAINTENGINE=1)
    elseif(CONFIG_REALTEK_BUILD_REAL_NANOVG)
        target_compile_definitions(honeygui PRIVATE CONFIG_REALTEK_BUILD_REAL_NANOVG=1)
    elseif(CONFIG_REALTEK_BUILD_REAL_BOX2D)
        target_compile_definitions(honeygui PRIVATE CONFIG_REALTEK_BUILD_REAL_BOX2D=1)
    elseif(CONFIG_REALTEK_BUILD_GUI_410_502_DEMO)
        target_compile_definitions(honeygui PRIVATE CONFIG_REALTEK_BUILD_GUI_410_502_DEMO=1)
    elseif(CONFIG_REALTEK_BUILD_GUI_466_466_DEMO)
        target_compile_definitions(honeygui PRIVATE CONFIG_REALTEK_BUILD_GUI_466_466_DEMO=1)
    elseif(CONFIG_REALTEK_BUILD_GUI_240_240_DEMO)
        target_compile_definitions(honeygui PRIVATE CONFIG_REALTEK_BUILD_GUI_240_240_DEMO=1)
    elseif(CONFIG_REALTEK_BUILD_GUI_800_480_DEMO)
        target_compile_definitions(honeygui PRIVATE CONFIG_REALTEK_BUILD_GUI_800_480_DEMO=1)
    elseif(CONFIG_REALTEK_BUILD_GUI_280_456_DEMO)
        target_compile_definitions(honeygui PRIVATE CONFIG_REALTEK_BUILD_GUI_280_456_DEMO=1)
    elseif(CONFIG_REALTEK_BUILD_GUI_320_384_DEMO)
        target_compile_definitions(honeygui PRIVATE CONFIG_REALTEK_BUILD_GUI_320_384_DEMO=1)
    elseif(CONFIG_REALTEK_BUILD_GUI_320_385_DEMO)
        target_compile_definitions(honeygui PRIVATE CONFIG_REALTEK_BUILD_GUI_320_385_DEMO=1)
    elseif(CONFIG_REALTEK_BUILD_HML_DESIGNER)
        target_compile_definitions(honeygui PRIVATE CONFIG_REALTEK_BUILD_HML_DESIGNER=1)
    elseif(CONFIG_REALTEK_BUILD_PARTICLE_DEMO)
        target_compile_definitions(honeygui PRIVATE CONFIG_REALTEK_BUILD_PARTICLE_DEMO=1)
    elseif(CONFIG_REALTEK_BUILD_TEST)
        target_compile_definitions(honeygui PRIVATE CONFIG_REALTEK_BUILD_TEST=1)
    elseif(CONFIG_REALTEK_BUILD_HONEYGUI_AUTO_TEST)
        target_compile_definitions(honeygui PRIVATE CONFIG_REALTEK_BUILD_HONEYGUI_AUTO_TEST=1)
    endif()

    # Feature configurations
    if(CONFIG_REALTEK_BUILD_LITE3D)
        target_compile_definitions(honeygui PRIVATE CONFIG_REALTEK_BUILD_LITE3D=1)
    endif()
    if(CONFIG_REALTEK_BUILD_GUI_BOX2D)
        target_compile_definitions(honeygui PRIVATE CONFIG_REALTEK_BUILD_GUI_BOX2D=1)
    endif()
    if(CONFIG_REALTEK_BUILD_PARTICLE_SYSTEM)
        target_compile_definitions(honeygui PRIVATE CONFIG_REALTEK_BUILD_PARTICLE_SYSTEM=1)
    endif()
    if(CONFIG_REALTEK_BUILD_XML_LOADER)
        target_compile_definitions(honeygui PRIVATE CONFIG_REALTEK_BUILD_XML_LOADER=1)
    endif()
    if(CONFIG_REALTEK_BUILD_LETTER_SHELL)
        target_compile_definitions(honeygui PRIVATE CONFIG_REALTEK_BUILD_LETTER_SHELL=1)
    endif()
    if(CONFIG_REALTEK_BUILD_MONKEY_TEST)
        target_compile_definitions(honeygui PRIVATE CONFIG_REALTEK_BUILD_MONKEY_TEST=1)
    endif()
    if(CONFIG_REALTEK_MINIMP3)
        target_compile_definitions(honeygui PRIVATE CONFIG_REALTEK_MINIMP3=1)
    endif()
    if(CONFIG_REALTEK_BUILD_AUDIO)
        target_compile_definitions(honeygui PRIVATE CONFIG_REALTEK_BUILD_AUDIO=1)
    endif()

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
target_sources(honeygui PRIVATE ${APP_SOURCES})
target_include_directories(honeygui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/application)

# Port layer sources
file(GLOB PORT_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../src/ports/realgui_port/*.c)
target_sources(honeygui PRIVATE ${PORT_SOURCES})
target_include_directories(honeygui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/ports/realgui_port)

# LCDC driver include
target_include_directories(honeygui PRIVATE ${REALTEK_SDK_BASE_PATH}/src/mcu/display/hardware/lcdc/inc)

# LCD device includes
target_include_directories(honeygui PRIVATE ${REALTEK_SDK_BASE_PATH}/src/app/Wearable/device/lcd/8773E)

# LCDC chip-specific device includes
target_include_directories(honeygui PRIVATE ${REALTEK_SDK_BASE_PATH}/src/mcu/display/hardware/lcdc/src/device/rtl8773e)

# Display hardware source files (LCDC driver + IDU + PPE acceleration)
set(DISPLAY_HW_DIR ${REALTEK_SDK_BASE_PATH}/src/mcu/display/hardware)
target_sources(honeygui PRIVATE ${DISPLAY_HW_DIR}/lcdc/src/device/rtl_common/rtl_lcdc.c)
target_sources(honeygui PRIVATE ${DISPLAY_HW_DIR}/lcdc/src/device/rtl_common/rtl_lcdc_edpi.c)
target_sources(honeygui PRIVATE ${DISPLAY_HW_DIR}/lcdc/src/device/rtl_common/rtl_lcdc_dbic.c)
target_sources(honeygui PRIVATE ${DISPLAY_HW_DIR}/lcdc/src/device/rtl_common/rtl_lcdc_dbib.c)
target_sources(honeygui PRIVATE ${DISPLAY_HW_DIR}/ppe/src/device/rtl8773e/rtl_ppe.c)
target_include_directories(honeygui PRIVATE ${DISPLAY_HW_DIR}/ppe/src/device/rtl8773e)
target_include_directories(honeygui PRIVATE ${DISPLAY_HW_DIR}/ppe/inc/rtl8773e)
target_include_directories(honeygui PRIVATE ${DISPLAY_HW_DIR}/ppe/inc)

# IDU hardware driver
target_sources(honeygui PRIVATE ${DISPLAY_HW_DIR}/idu/src/device/rtl_common/rtl_idu.c)
target_sources(honeygui PRIVATE ${DISPLAY_HW_DIR}/idu/src/device/rtl8773e/rtl_idu_int.c)
target_include_directories(honeygui PRIVATE ${DISPLAY_HW_DIR}/idu/src/device/rtl8773e)
target_include_directories(honeygui PRIVATE ${DISPLAY_HW_DIR}/idu/inc)

# PPE acceleration binding (HoneyGUI hardware engine)
set(HONEYGUI_BINDING_DIR ${REALTEK_SDK_BASE_PATH}/src/mcu/display/bindings/honeygui/hardware_engine)
target_sources(honeygui PRIVATE ${HONEYGUI_BINDING_DIR}/PPE/rtl8773e/acc_ppe.c)

# Device driver sources (from Wearable)
set(DEVICE_SRC_DIR ${REALTEK_SDK_BASE_PATH}/src/app/Wearable)
if(CONFIG_REALTEK_KEY_BUTTON_8773E)
    target_sources(honeygui PRIVATE ${DEVICE_SRC_DIR}/device/key_button/8773E/key_button_8773e.c)
    target_include_directories(honeygui PRIVATE ${DEVICE_SRC_DIR}/device/key_button/8773E)
endif()

if(CONFIG_REALTEK_LCD_ST7265_800_480_RGB)
    target_sources(honeygui PRIVATE ${DEVICE_SRC_DIR}/device/lcd/8773E/lcd_st7265_800480_rgb.c)
    target_include_directories(honeygui PRIVATE ${DEVICE_SRC_DIR}/device/lcd/8773E)
endif()

# LCD driver
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
