set(POSIX_DEVICE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../posix-device)


target_sources(app PRIVATE
    ${POSIX_DEVICE_ROOT}/core/posix_device.c
    ${POSIX_DEVICE_ROOT}/core/posix_init.c
)

target_include_directories(app PUBLIC
    ${POSIX_DEVICE_ROOT}/include
    ${POSIX_DEVICE_ROOT}/include/ioctls
)

# target_compile_definitions(app PRIVATE
#     POSIX_DEVICE_TABLE_SIZE=${CONFIG_POSIX_DEVICE_TABLE_SIZE}
#     POSIX_FD_POOL_SIZE=${CONFIG_POSIX_DEVICE_FD_POOL_SIZE}
# )

zephyr_linker_sources(SECTIONS ${CMAKE_CURRENT_SOURCE_DIR}/posix_init_zephyr.ld)

add_subdirectory(custom-rtos)



add_subdirectory(examples)
