import os
import sys
import stat
# toolchains options
ARCH='arm'
CPU='cortex-m4'
CROSS_TOOL='gcc'

# bsp lib config
BSP_LIBRARY_TYPE = None

if os.getenv('RTT_CC'):
    CROSS_TOOL = os.getenv('RTT_CC')

RTT_ROOT = os.path.normpath(os.getcwd() + '../../rt-thread')

# cross_tool provides the cross compiler
# EXEC_PATH is the compiler execute path.
# gcc: set env ARM_GCC to the toolchain bin dir to override the default below.
if  CROSS_TOOL == 'gcc':
    PLATFORM    = 'gcc'
    if os.name == 'nt':
        EXEC_PATH = os.environ.get('ARM_GCC') or \
            r'C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/13.2 Rel1/bin'
    else:
        EXEC_PATH = os.environ.get('ARM_GCC') or \
            r'/opt/arm-gnu-toolchain-12.2.rel1-x86_64-arm-none-eabi/bin'
elif CROSS_TOOL == 'keil':
    PLATFORM    = 'armcc'
    EXEC_PATH   = r'C:/Keil_v5'
elif CROSS_TOOL == 'iar':
    PLATFORM    = 'iar'
    EXEC_PATH   = r'C:/Program Files (x86)/IAR Systems/Embedded Workbench 8.0'

if os.getenv('RTT_EXEC_PATH'):
    EXEC_PATH = os.getenv('RTT_EXEC_PATH')

# release (-O2) so the benchmark numbers are meaningful
BUILD = 'release'
LIBDIR = ' -lm  -lc -specs=nosys.specs'

if PLATFORM == 'gcc':
    # toolchains
    PREFIX = 'arm-none-eabi-'
    CC = PREFIX + 'gcc'
    AS = PREFIX + 'gcc'
    AR = PREFIX + 'ar'
    CXX = PREFIX + 'g++'
    LINK = PREFIX + 'g++'
    TARGET_EXT = 'elf'
    SIZE = PREFIX + 'size'
    OBJDUMP = PREFIX + 'objdump'
    OBJCPY = PREFIX + 'objcopy'

    # RTL87x2G core (same as the other rtl87x2g gcc apps, e.g. applications/86box)
    DEVICE = ' -march=armv8.1-m.main+dsp+mve+fp -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections'
    CFLAGS = DEVICE + ' -Dgcc' + ' -Wno-implicit-function-declaration'
    AFLAGS = ' -c' + DEVICE + ' -x assembler-with-cpp -Wa,-mimplicit-it=thumb '
    # gcc/app.ld is run through the C preprocessor by SConstruct -> gcc/app.ld.gen
    LFLAGS = DEVICE + ' -T gcc/app.ld.gen ' + LIBDIR + ' -Wl,--gc-sections,-Map=app.map,-cref,-u,Reset_Handler ' + ' -Wl,--no-warn-rwx-segments'

    CPATH = ''
    LPATH = ''

    if BUILD == 'debug':
        CFLAGS += ' -O0 -gdwarf-2'
        AFLAGS += ' -gdwarf-2'
    else:
        CFLAGS += ' -O2 -g'

    CXXFLAGS = CFLAGS.replace(' -Wno-implicit-function-declaration', '')

    # app.bin -> app_MP.bin (with MP header, flashable by MPTool / MPPackTool)
    POST_ACTION = OBJCPY + ' -O binary $TARGET app.bin --remove-section=App.trace\n'
    POST_ACTION += OBJCPY + ' -O binary $TARGET app.trace --only-section=App.trace\n'

    # paths are relative to proj/ (applications/lvgl_8772g/proj -> sdk root = ../../../)
    if os.name == 'nt':
        PREPEND = '..\\..\\..\\tools\\prepend_header\\prepend_header.exe -t app_code -b 15 -p app.bin'
        PEM = '..\\..\\..\\tools\\keys\\rtk_ecdsa.pem -a ..\\..\\..\\tools\\keys\\key.json'
        MD5 = '..\\..\\..\\tools\\md5\\md5.exe'
        POST_ACTION += PREPEND + ' -m 1 -i .\\mp.ini -r ' + PEM + ' \n'
    else:
        os.chmod("../../../tools/prepend_header/prepend_header", stat.S_IRWXU)
        os.chmod("../../../tools/md5/md5", stat.S_IRWXU)
        PREPEND = '../../../tools/prepend_header/prepend_header -t app_code -b 15 -p app.bin'
        PEM = '../../../tools/keys/rtk_ecdsa.pem -a ../../../tools/keys/key.json'
        MD5 = '../../../tools/md5/md5'
        POST_ACTION += PREPEND + ' -m 1 -i ./mp.ini -r ' + PEM + ' \n'

    POST_ACTION += SIZE + ' $TARGET \n'
    POST_ACTION += MD5 + ' app_MP.bin \n'

elif PLATFORM == 'armcc':
    # toolchains
    CC = 'armcc'
    CXX = 'armcc'
    AS = 'armasm'
    AR = 'armar'
    LINK = 'armlink'
    TARGET_EXT = 'axf'

    DEVICE = ' --cpu Cortex-M4'
    CFLAGS = DEVICE + ' --c99 --apcs=interwork'
    AFLAGS = DEVICE
    LFLAGS = DEVICE + ' --info sizes --info totals --info unused --info veneers --list rtthread-apollo2.map --scatter rtthread.sct'

    LFLAGS += ' --keep *.o(.rti_fn.*)   --keep *.o(FSymTab) --keep *.o(VSymTab)'

    EXEC_PATH += '/ARM/ARMCC/bin'

    if BUILD == 'debug':
        CFLAGS += ' -g -O0'
        AFLAGS += ' -g'
    else:
        CFLAGS += ' -O2'

    CXXFLAGS = CFLAGS
    CFLAGS += ' -std=c99'

    POST_ACTION = 'fromelf --bin $TARGET --output rtthread.bin \nfromelf -z $TARGET'
