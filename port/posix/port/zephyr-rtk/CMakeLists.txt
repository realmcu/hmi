# posix-device abstraction layer - RTK8773G + Zephyr port
set(POSIX_DEVICE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../posix-device)

target_sources(app PRIVATE
    # posix-device core (platform-independent)
    ${POSIX_DEVICE_ROOT}/core/posix_device.c
    ${POSIX_DEVICE_ROOT}/core/posix_init.c
    # RTK8773G + Zephyr port drivers
    ${CMAKE_CURRENT_SOURCE_DIR}/posix_port_init.c
    ${CMAKE_CURRENT_SOURCE_DIR}/posix_port_lcd.c
    ${CMAKE_CURRENT_SOURCE_DIR}/posix_port_touch.c
    ${CMAKE_CURRENT_SOURCE_DIR}/posix_port_gsensor.c
    ${CMAKE_CURRENT_SOURCE_DIR}/posix_port_gpio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/posix_port_uart.c
    ${CMAKE_CURRENT_SOURCE_DIR}/posix_port_spi.c
)

target_include_directories(app PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}          # posix_port.h
    ${POSIX_DEVICE_ROOT}/include         # posix.h, posix_device.h, etc.
    ${POSIX_DEVICE_ROOT}/include/ioctls  # per-device ioctl headers
)

zephyr_linker_sources(SECTIONS ${CMAKE_CURRENT_SOURCE_DIR}/posix_init.ld)
