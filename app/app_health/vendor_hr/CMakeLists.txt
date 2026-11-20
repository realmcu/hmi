# Copyright (c) 2026 Realtek Semiconductor Corp.
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.10)

# The app target is created by the application root; this subdirectory adds
# the vendor archive to it. Keep the cross-directory link explicit.
if(POLICY CMP0079)
    cmake_policy(SET CMP0079 NEW)
endif()

set(VC_HR_LIB_SRC "${CMAKE_CURRENT_SOURCE_DIR}/lib_hr_H102ed20_KeilM4_NA_01.lib")
set(VC_HR_LIB_OUT "${CMAKE_CURRENT_BINARY_DIR}/libvc_hr_algo.a")

# The vendor archive is an ARMCC object archive. Re-home its read-only data
# section so the Zephyr linker script places it with .rodata, and drop the
# ARMCC-only linker metadata section.
add_custom_command(
    OUTPUT "${VC_HR_LIB_OUT}"
    COMMAND ${CMAKE_OBJCOPY}
            --rename-section .constdata=.rodata.vc_hr_constdata
            --remove-section .arm_vfe_header
            "${VC_HR_LIB_SRC}" "${VC_HR_LIB_OUT}"
    DEPENDS "${VC_HR_LIB_SRC}"
    COMMENT "Importing Vcare HR algorithm archive"
    VERBATIM
)
add_custom_target(vc_hr_algo_archive DEPENDS "${VC_HR_LIB_OUT}")

add_library(vc_hr_algo STATIC IMPORTED GLOBAL)
set_target_properties(vc_hr_algo PROPERTIES IMPORTED_LOCATION "${VC_HR_LIB_OUT}")
add_dependencies(vc_hr_algo vc_hr_algo_archive)

target_include_directories(${APP_TARGET_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(${APP_TARGET_NAME} PRIVATE vc_hr_algo)
