# Copyright (c) 2026 HoneyGUI / eBadge team
# SPDX-License-Identifier: Apache-2.0
#
# eBadge Protocol V1.2 stack (see PROT-001).  Replaces the old
# component/protocol/ + app/l2_handlers/ layers, which are excluded from
# the build via emptied CMakeLists in those directories.
#
# Layout:
#   app/protocol/
#     ebadge_*                -- framing / L2 / task / task-tick timer
#     handlers/               -- one .c per V1.2 command
#     wifi_xfer/              -- xfer_session + EBXF/EBXR + tcp_server
#     port/                   -- transport-agnostic porting stubs

cmake_minimum_required(VERSION 3.10)

if(1)
    # Root-level sources (ebadge_frame.c / ebadge_l2.c / ebadge_task.c)
    file(GLOB EB_ROOT_SRC "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
    target_sources(${APP_TARGET_NAME} PRIVATE ${EB_ROOT_SRC})

    # Handlers
    file(GLOB EB_HANDLER_SRC "${CMAKE_CURRENT_SOURCE_DIR}/handlers/*.c")
    target_sources(${APP_TARGET_NAME} PRIVATE ${EB_HANDLER_SRC})

    # Wi-Fi xfer plane
    file(GLOB EB_XFER_SRC "${CMAKE_CURRENT_SOURCE_DIR}/wifi_xfer/*.c")
    target_sources(${APP_TARGET_NAME} PRIVATE ${EB_XFER_SRC})

    # Port abstraction
    file(GLOB EB_PORT_SRC "${CMAKE_CURRENT_SOURCE_DIR}/port/*.c")
    target_sources(${APP_TARGET_NAME} PRIVATE ${EB_PORT_SRC})

    # Public include -- headers reference each other by relative path, so
    # exposing the root is enough for callers (main.c) and the sub-.c files.
    target_include_directories(${APP_TARGET_NAME} PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/handlers
        ${CMAKE_CURRENT_SOURCE_DIR}/wifi_xfer
        ${CMAKE_CURRENT_SOURCE_DIR}/port
    )
endif()
