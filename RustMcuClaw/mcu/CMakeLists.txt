# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)

# Keep the same Realtek post-build flow used by the other RTL87X3G apps.
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../../cmake/post_build.cmake)
rtk_post_build()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(rustmcuclaw_mcu)

set(APP_TARGET_NAME app)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/mp.ini ${CMAKE_BINARY_DIR}/../mp.ini COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/VERSION ${CMAKE_BINARY_DIR}/../VERSION COPYONLY)

zephyr_include_directories(
    include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../watch/src/module/clk
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../modules/wearable/device/lcd/8773G
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../modules/display/device/touch/8773G
)
target_sources(app PRIVATE
    src/main.c
    src/psram_init.c
    src/ws2812_strip.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../watch/src/module/clk/clk_mgr.c
)

target_link_libraries(app PRIVATE realtek_wearable_library)
target_link_libraries(app PRIVATE realtek_display_driver_library)

find_program(CARGO_EXECUTABLE cargo)
if(NOT CARGO_EXECUTABLE)
    message(FATAL_ERROR "cargo not found in PATH. Install Rust and run: rustup target add thumbv8m.main-none-eabihf")
endif()

set(RUST_WORKSPACE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(RUST_CRATE_DIR ${RUST_WORKSPACE_DIR}/crates/rustmcuclaw-mcu)
set(RUST_TARGET thumbv8m.main-none-eabihf)
set(RUST_TARGET_DIR ${CMAKE_CURRENT_BINARY_DIR}/cargo-target)
set(RUST_STATIC_LIB ${RUST_TARGET_DIR}/${RUST_TARGET}/release/librustmcuclaw_mcu.a)

string(TOUPPER "${RUST_TARGET}" RUST_TARGET_ENV)
string(REPLACE "-" "_" RUST_TARGET_ENV "${RUST_TARGET_ENV}")
string(REPLACE "." "_" RUST_TARGET_ENV "${RUST_TARGET_ENV}")

# Re-glob every Rust source under crates/ so a change in any workspace member
# (rustmcuclaw-mcu, rustmcuclaw-common, future crates) re-triggers cargo. The
# previous version only listed rustmcuclaw-mcu/src/lib.rs which silently
# skipped relinking when only common-crate prompts/constants had changed.
file(GLOB_RECURSE RUST_SOURCES CONFIGURE_DEPENDS
    ${RUST_WORKSPACE_DIR}/crates/*.rs
    ${RUST_WORKSPACE_DIR}/crates/*/Cargo.toml
)

add_custom_command(
    OUTPUT ${RUST_STATIC_LIB}
    COMMAND ${CMAKE_COMMAND} -E env
            CARGO_TARGET_DIR=${RUST_TARGET_DIR}
            CARGO_TARGET_${RUST_TARGET_ENV}_LINKER=${CMAKE_C_COMPILER}
            RUSTFLAGS=-Cpanic=abort\ -Cforce-unwind-tables=no
            ${CARGO_EXECUTABLE}
            build
            --manifest-path ${RUST_CRATE_DIR}/Cargo.toml
            --target ${RUST_TARGET}
            --release
    WORKING_DIRECTORY ${RUST_WORKSPACE_DIR}
    DEPENDS
        ${RUST_WORKSPACE_DIR}/Cargo.toml
        ${RUST_SOURCES}
    COMMENT "Building RustMcuClaw MCU Rust static library"
    VERBATIM
)

add_custom_target(rustmcuclaw_mcu_staticlib ALL DEPENDS ${RUST_STATIC_LIB})
add_dependencies(app rustmcuclaw_mcu_staticlib)
target_link_libraries(app PRIVATE ${RUST_STATIC_LIB})

zephyr_compile_options(
    -Wno-unused-parameter
)

# ADMA2 descriptors and DMA data buffers must be in DMA-accessible SRAM.
# The default BSS placement is DTCM (0x2000B800) which is not reachable by
# the SDIO DMA engine.  Force both translation units into SRAM2.
zephyr_code_relocate(FILES
    ${ZEPHYR_BASE}/drivers/sdhc/sdhc_rtl87x3g.c
    LOCATION SRAM)
zephyr_code_relocate(FILES
    ${ZEPHYR_BASE}/drivers/disk/sdmmc_subsys.c
    LOCATION SRAM)
