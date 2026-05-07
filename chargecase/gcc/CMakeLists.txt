#
# Copyright (c) 2026, Realtek Semiconductor Corporation
#
# SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
#
message([STATUS] "[APP]honeygui cmake start build")
set(CMAKE_EXECUTABLE_SUFFIX ".elf")
enable_language(ASM)
add_executable(honeygui " ")
set(build_target_list ${build_target_list} "honeygui" CACHE INTERNAL "build target list")

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/bin/${bin_output_path})
file(GLOB OUT_FILES   ${CMAKE_CURRENT_SOURCE_DIR}/bin/${bin_output_path}/*MP*.bin)
if(NOT "${OUT_FILES}" STREQUAL "")
file(REMOVE ${OUT_FILES})
endif()
endif()

set_target_properties(honeygui PROPERTIES
                      ARCHIVE_OUTPUT_DIRECTORY ${lib_output_path}
                      LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
                      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin/${bin_output_path}
                      OUTPUT_NAME honeygui_${build_bank})
  

#remove to sdk/bin

target_link_libraries(honeygui
                      PUBLIC ${lib_gap}
                      PUBLIC ${lib_rom}
                      PUBLIC ${lib_upperstack}
                      PUBLIC halLib
                      PUBLIC filesystemLib
                      PUBLIC sysmLib
                      PUBLIC romLib
                      PUBLIC upperstackLib                      
                      )
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../src/gui_lib/include)
target_link_libraries(honeygui  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../src/gui_lib/gcc/libgui.a)                     
if(compile_lib)

target_link_libraries(honeygui
                      PUBLIC ${halLib_link}
                      PUBLIC ${filesystemLib_link}
                      PUBLIC ${sysmLib_link}
                      )
else()
#  target_link_libraries(honeygui      PUBLIC malluesLib)   
endif()
#preinclude kconfig.h
target_precompile_headers(honeygui PRIVATE  ${kconfig_header_dir_path}/config.h)
target_include_directories(honeygui PRIVATE ${kconfig_header_dir_path})
target_compile_definitions(honeygui PRIVATE CONFIG_REALTEK_APP_HONEYGUI_DEMO_SUPPORT=1)
target_compile_definitions(honeygui PRIVATE TARGET_RTL8773E=1
                                            CONFIG_SOC_SERIES_RTL8773E=1
                                            SUPPORT_ACCESS_SHM=1
                                            RTK_MODULE_RTK_PPEV2
                                            )

#include directory for board.h config.h
target_include_directories(honeygui PUBLIC ../)
target_include_directories(honeygui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(honeygui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../inc/rtl87x3ep)
target_include_directories(honeygui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../mdk)


#sdk/inc/app
target_include_directories(honeygui PRIVATE ${sdk_app_dir_path})

target_compile_definitions(honeygui PUBLIC RAM_TYPE_DATA_ON=0)

#boot file
target_sources(honeygui
               PRIVATE ${realtek_start_up_boot_file}
               PRIVATE ${arm_start_up_boot_file})
set_property(SOURCE ${arm_start_up_boot_file} PROPERTY LANGUAGE C)

#generate version.h for app
set(version_path ${CMAKE_CURRENT_SOURCE_DIR}/../version.h)
generate_version("honeygui_app" ${version_path})


#set link script app.ld
generate_ldscript("${CMAKE_CURRENT_SOURCE_DIR}/app.ld" honeygui)

#app link option
set(LINK_FLAGS "-fdata-sections -ffunction-sections -Wl,--gc-sections")
set(EXTRA_LINK_FLAGS "-Wl,-Map=${CMAKE_CURRENT_SOURCE_DIR}/bin/${bin_output_path}/honeygui_${build_bank}.map,--cref,--no-warn-mismatch -specs=nosys.specs")
#app compile option
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

# post build
set(gcc_tool_path ${REALTEK_SDK_BASE_PATH}/tool/Gadgets/gcc_tool)
set(debug_table_tool_path ${REALTEK_SDK_BASE_PATH}/tool/Gadgets/build_debug_table_tool)

add_custom_command(TARGET  honeygui POST_BUILD
            WORKING_DIRECTORY  ${CMAKE_CURRENT_LIST_DIR}/bin/${bin_output_path}
            COMMAND ${CMAKE_OBJCOPY} -O ihex   ${CMAKE_CURRENT_SOURCE_DIR}/bin/${bin_output_path}/honeygui_${build_bank}.elf  ${CMAKE_CURRENT_SOURCE_DIR}/bin/${bin_output_path}/honeygui_${build_bank}.hex
            COMMAND ${gcc_tool_path}/Hex2Bin.exe  ${CMAKE_CURRENT_SOURCE_DIR}/bin/${bin_output_path}/honeygui_${build_bank}.hex ${CMAKE_CURRENT_SOURCE_DIR}/bin/${bin_output_path}/honeygui_${build_bank}.bin
            COMMAND ${gcc_tool_path}/prepend_header.exe /app_code ${CMAKE_CURRENT_SOURCE_DIR}/bin/${bin_output_path}/honeygui_${build_bank}.bin   /ecdsa ${gcc_tool_path}/../ecdsa_key.pem
            COMMAND ${gcc_tool_path}/prepend_header.exe /app_code ${CMAKE_CURRENT_SOURCE_DIR}/bin/${bin_output_path}/honeygui_${build_bank}.bin   /mp_ini ${CMAKE_CURRENT_SOURCE_DIR}/../mdk/mp.ini
            COMMAND bash ${gcc_tool_path}/md5_generate.sh ${CMAKE_CURRENT_SOURCE_DIR}/bin/${bin_output_path}/honeygui_${build_bank}_MP.bin ${version_path}
            COMMAND ${debug_table_tool_path}/build_debug_table.exe --debug_dir ${CMAKE_CURRENT_SOURCE_DIR}/bin/${bin_output_path} --su_dir ${CMAKE_CURRENT_SOURCE_DIR}/../../../../build --out_dir ${CMAKE_CURRENT_SOURCE_DIR}/bin/${bin_output_path}
            COMMAND ${CMAKE_OBJDUMP} -D -S ${CMAKE_CURRENT_SOURCE_DIR}/bin/${bin_output_path}/honeygui_${build_bank}.elf > ${CMAKE_CURRENT_SOURCE_DIR}/bin/${bin_output_path}/honeygui_${build_bank}.disasm
            )

