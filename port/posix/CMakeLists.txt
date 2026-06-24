set(POSIX_DEVICE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../component/posix)
set(POSIX_DEVICE_PORT ${POSIX_DEVICE_ROOT}/examples/port/custom-rtos)
set(POSIX_DEVICE_DEMO ${POSIX_DEVICE_ROOT}/examples/demo)

target_sources(app PRIVATE
    ${POSIX_DEVICE_ROOT}/core/posix_device.c
    ${POSIX_DEVICE_ROOT}/core/posix_init.c
)

file(GLOB POSIX_DEVICE_PORT_SOURCES ${POSIX_DEVICE_PORT}/*.c)
file(GLOB POSIX_DEVICE_DEMO_SOURCES ${POSIX_DEVICE_DEMO}/*.c)
target_sources(app PRIVATE ${POSIX_DEVICE_PORT_SOURCES} ${POSIX_DEVICE_DEMO_SOURCES})

target_include_directories(app PUBLIC
    ${POSIX_DEVICE_ROOT}/include
    ${POSIX_DEVICE_ROOT}/include/ioctls
    ${POSIX_DEVICE_PORT}
)

zephyr_linker_sources(SECTIONS ${CMAKE_CURRENT_SOURCE_DIR}/posix_init_zephyr.ld)
