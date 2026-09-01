# Copyright (c) 2024 Realtek Semiconductor Corp.
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.10)


# Define a variable for the library name
if(1) #set to 1 to enable or variable to for depnedency

    file(GLOB SOURCES "*.c" "*.cpp")
    file(GLOB HEADERS "*.h" "*.hpp")

    # hmi_ble_central.c is compiled again (BLE scanning).  It used to be removed
    # here wholesale because its SENDER half is bound to the pre-V1.2 protocol in
    # app/l2_handlers/, which is not built and not on the include path.  That half
    # is now behind HMI_CENTRAL_ENABLE_SENDER (default 0, see hmi_ble_central.h);
    # the SCANNER half has no such dependency -- it uses only the ROM-resident
    # observer API -- so excluding the whole file to park the sender also threw away
    # scanning, which is a separate feature.  Re-enabling the sender means building
    # app/l2_handlers/ first, NOT touching this file.

    # Determine if we should create a library
    if((NOT HEADERS) AND (NOT SOURCES))
        message(STATUS "No source or header files found in ${CMAKE_CURRENT_SOURCE_DIR}.")
		target_include_directories(${APP_TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    endif()

    if(HEADERS AND (NOT SOURCES))
        target_include_directories(${APP_TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    endif()

    if(SOURCES AND (NOT HEADERS))
        target_sources(${APP_TARGET_NAME} PRIVATE ${SOURCES})
    endif()

    if(SOURCES AND HEADERS)
        target_sources(${APP_TARGET_NAME} PRIVATE ${SOURCES})
        target_include_directories(${APP_TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    endif()

    # Process subdirectories regardless
    file(GLOB SUBDIRS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/*)

    foreach(subdir ${SUBDIRS})
        if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${subdir})
            if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${subdir}/CMakeLists.txt)
                add_subdirectory(${subdir})
            else()
                message(STATUS "Skipping directory ${subdir} as it does not contain a CMakeLists.txt file.")
            endif()
        endif()
    endforeach()
endif()
