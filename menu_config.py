#
# Copyright (c) 2026, Realtek Semiconductor Corporation
#
# SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
#
import os

# Define cross-compiler toolchain settings
ARCH = 'arm'
CPU = 'cortex-m3'
CROSS_TOOL = 'armcc'

# bsp lib configuration
BSP_LIBRARY_TYPE = None

# Read configuration from environment variables
if os.getenv('RTT_CC'):
    CROSS_TOOL = os.getenv('RTT_CC')
if os.getenv('RTT_ROOT'):
    RTT_ROOT = os.getenv('RTT_ROOT')

RTT_ROOT = os.path.normpath(os.getcwd() + '../../rt-thread')

# Set platform and exec path based on cross-compiler toolchain
if CROSS_TOOL == 'gcc':
    PLATFORM = 'gcc'
    EXEC_PATH = r'/usr/bin'
elif CROSS_TOOL == 'armcc':
    PLATFORM = 'armcc'
    EXEC_PATH = r'C:/Keil_v5/ARM/ARMCC/bin'  # Update this path to point to ARM Compiler 5
elif CROSS_TOOL == 'iar':
    PLATFORM = 'iar'
    EXEC_PATH = r'C:/Program Files (x86)/IAR Systems/Embedded Workbench 8.0'

if os.getenv('RTT_EXEC_PATH'):
    EXEC_PATH = os.getenv('RTT_EXEC_PATH')

BUILD = 'debug'

if PLATFORM == 'gcc':
    PREFIX = 'arm-none-eabi-'
    CC = PREFIX + 'gcc'
    AS = PREFIX + 'gcc'
    AR = PREFIX + 'ar'
    CXX = PREFIX + 'g++'
    LINK = PREFIX + 'gcc'
    TARGET_EXT = 'elf'
    SIZE = PREFIX + 'size'
    OBJDUMP = PREFIX + 'objdump'
    OBJCOPY = PREFIX + 'objcopy'
    
    DEVICE = ' -mcpu=cortex-m3 -mthumb -ffunction-sections -fdata-sections'
    CFLAGS = DEVICE + ' -Wall -Wno-implicit-function-declaration'
    AFLAGS = ' -c' + DEVICE + ' -x assembler-with-cpp -Wa,-mimplicit-it=thumb'
    LFLAGS = DEVICE + ' -Wl,--gc-sections,-Map=rtthread.map,-cref,-u,Reset_Handler -T board/link_scripts/link.lds'
    
    CPATH = ''
    LPATH = ''
    
    if BUILD == 'debug':
        CFLAGS += ' -O0 -gdwarf-3'
        AFLAGS += ' -gdwarf-3'
    else:
        CFLAGS += ' -O2'
    
    CXXFLAGS = CFLAGS
    
    POST_ACTION = OBJCOPY + ' -O binary $TARGET rtthread.bin\n' + SIZE + ' $TARGET \n'
    
elif PLATFORM == 'armcc':
    # Toolchain-related settings
    CC = 'armcc'
    CXX = 'armcc'
    AS = 'armasm'
    AR = 'armar'
    LINK = 'armlink'
    TARGET_EXT = 'axf'
    
    DEVICE = ' --cpu Cortex-M3'
    CFLAGS = DEVICE + ' --c99 --apcs=interwork'
    AFLAGS = DEVICE
    LFLAGS = DEVICE + ' --info sizes --info totals --info unused --info veneers --list rtthread-apollo2.map --scatter rtthread.sct'
    
    LFLAGS += ' --keep *.o(.rti_fn.*) --keep *.o(FSymTab) --keep *.o(VSymTab)'

    EXEC_PATH += '/ARM/ARMCC/bin'
    
    if BUILD == 'debug':
        CFLAGS += ' -g -O0 -D__MICROLIB'  # Add -D__MICROLIB
        AFLAGS += ' -g'
    else:
        CFLAGS += ' -O2'
    
    CXXFLAGS = CFLAGS 
    CFLAGS += ' --implicit-type'

    # Make sure there are no unrecognized -D options
    # Adjust macro definitions to avoid spaces and use the correct define format
    CFLAGS += ' -D PROJECT_VERSION="1.0.0"'
    
    POST_ACTION = 'fromelf --bin $TARGET --output rtthread.bin \nfromelf -z $TARGET'

# Print compiler settings for debugging
print("CC: ", CC)
print("CFLAGS: ", CFLAGS)
print("AFLAGS: ", AFLAGS)
print("LFLAGS: ", LFLAGS)
