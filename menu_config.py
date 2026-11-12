#
# Copyright (c) 2026, Realtek Semiconductor Corporation
#
# SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
#
"""Toolchain description for the SCons build of this board.

Only the tools and where they live belong here; the compile and link options
come from mdk/project.uvprojx (see SConstruct), so both builds share a single
description of what is built.

The Keil project uses Arm Compiler 6 (armclang / armlink) and the SoC is a
Cortex-M33 with a single-precision FPU, so the SCons build drives the same
toolchain -- anything else produces a different image.
"""

import os

ARCH = 'arm'
CPU = 'cortex-m33'
CROSS_TOOL = 'keil'

if os.getenv('RTT_CC'):
    CROSS_TOOL = os.getenv('RTT_CC')

if CROSS_TOOL != 'keil':
    raise SystemExit('hmi_dashboard is built with Arm Compiler 6; '
                     'set RTT_CC=keil or leave it unset')

PLATFORM = 'armclang'

# Arm Compiler 6 as installed by MDK. Override with RTT_EXEC_PATH if Keil lives
# somewhere else.
EXEC_PATH = os.getenv('RTT_EXEC_PATH') or r'C:/Keil_v5/ARM/ArmCompilerforEmbedded6.22/bin'

CC = 'armclang'
CXX = 'armclang'
AS = 'armclang'
AR = 'armar'
LINK = 'armlink'
FROMELF = 'fromelf'
TARGET_EXT = 'axf'

# Filled in by SConstruct from the project file, so nothing can silently fall
# back to the stock RT-Thread armcc settings.
CFLAGS = ''
AFLAGS = ''
LFLAGS = ''
