# ================================================================
# posix-device integration
#
# Mirrors the style of eBadge_8773g_dev/port/posix (the newer, working integration logic).
# The sources use this project's bundled component/posix-device -- the same
# gitee repo (realmcu_admin/posix-device) as the component/posix referenced by dev,
# at the latest version (gsensor/touch verified).
#
# Data flow: /dev/gsensor0 --alias--> /dev/sc7a20 --> /dev/i2c0 --> SC7A20(0x18)
# ================================================================
set(POSIX_DEVICE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../component/posix-device)
set(POSIX_DEVICE_PORT_COMMON ${POSIX_DEVICE_ROOT}/examples/port/common)
set(POSIX_DEVICE_PORT ${POSIX_DEVICE_ROOT}/examples/port/zephyr-rtk)
set(POSIX_DEVICE_DEMO ${POSIX_DEVICE_ROOT}/examples/demo)

target_sources(app PRIVATE
    ${POSIX_DEVICE_ROOT}/core/posix_device.c
    ${POSIX_DEVICE_ROOT}/core/posix_init.c
)

file(GLOB POSIX_DEVICE_PORT_SOURCES ${POSIX_DEVICE_PORT}/*.c)
file(GLOB POSIX_DEVICE_PORT_COMMON_SOURCES ${POSIX_DEVICE_PORT_COMMON}/*.c)
file(GLOB POSIX_DEVICE_DEMO_SOURCES ${POSIX_DEVICE_DEMO}/*.c)
target_sources(app PRIVATE
    ${POSIX_DEVICE_PORT_SOURCES}
    ${POSIX_DEVICE_PORT_COMMON_SOURCES}
    ${POSIX_DEVICE_DEMO_SOURCES}
)

target_include_directories(app PUBLIC
    ${POSIX_DEVICE_ROOT}/include
    ${POSIX_DEVICE_ROOT}/include/ioctls
    ${POSIX_DEVICE_ROOT}/examples/port
    ${POSIX_DEVICE_PORT}
    ${POSIX_DEVICE_PORT_COMMON}
)

zephyr_linker_sources(SECTIONS ${CMAKE_CURRENT_SOURCE_DIR}/posix_init_zephyr.ld)
