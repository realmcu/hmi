# ================================================================
# posix-device 接入
#
# 写法对齐 eBadge_8773g_dev/port/posix（新的、可用的接入逻辑）。
# 源使用本工程自带的 component/posix-device —— 与 dev 引用的
# component/posix 是同一个 gitee 仓库(realmcu_admin/posix-device)的
# 最新版（gsensor/touch 已验证）。
#
# 数据流: /dev/gsensor0 --alias--> /dev/sc7a20 --> /dev/i2c0 --> SC7A20(0x18)
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
