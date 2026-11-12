#
# Copyright (c) 2026, Realtek Semiconductor Corporation
#
# SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
#
"""SCons entry point for hmi_dashboard.

The build description is mdk/project.uvprojx: the source list, include paths,
preprocessor defines, library list, scatter file and compiler options are read
from the Keil project, so `scons` and uVision compile the same files with the
same flags and link the same libraries. Maintaining two hand-written copies is
what previously let the two builds drift apart.

    scons                        # target dashboard_dual_bank0
    scons TARGET=dashboard_dual_bank1

Outputs land in build_scons/: dashboard.axf, dashboard.bin, dashboard.map.
"""

import os
import xml.etree.ElementTree as ET

import menu_config

BSP_DIR = Dir('.').abspath
MDK_DIR = os.path.join(BSP_DIR, 'mdk')
PROJECT_FILE = os.path.join(MDK_DIR, 'project.uvprojx')
OUT_DIR = os.path.join(BSP_DIR, 'build_scons')

TARGET_NAME = ARGUMENTS.get('TARGET', 'dashboard_dual_bank0')

# Arm Compiler 6 options for a Cortex-M33 with a single-precision FPU. They
# mirror the target options in project.uvprojx: device, C standard, short
# enums and wchar, one section per function, LTO, optimisation level.
DEVICE_FLAGS = ('--target=arm-arm-none-eabi -mcpu=cortex-m33 '
                '-mfpu=fpv5-sp-d16 -mfloat-abi=hard')
COMPILE_FLAGS = DEVICE_FLAGS + (' -std=gnu11 -fshort-enums -fshort-wchar'
                                ' -ffunction-sections -fdata-sections -flto -O3 -g')

FORCED_INCLUDES = ['pre_define.h', 'stdbool.h', 'app_flags.h']


def to_abs(path):
    """Resolve a path as written in the Keil project (relative to mdk/)."""
    return os.path.normpath(os.path.join(MDK_DIR, path.replace('\\', '/')))


def cmsis_required_version():
    """Version of the CMSIS CORE component the project asks for, e.g. 5.6.0."""
    root = ET.parse(PROJECT_FILE).getroot()
    for comp in root.iter('component'):
        if comp.get('Cclass') == 'CMSIS' and comp.get('Cgroup') == 'CORE':
            return comp.get('Cversion')
    return None


def find_cmsis_core():
    """Locate the CMSIS Core headers the project pulls in as an RTE component.

    uVision resolves the RTE pack behind the scenes, so the path never appears
    in the project's include list. core_cm4.h comes from there and rtl876x.h
    includes it unconditionally, so the SCons build needs it too.

    The version matters: uVision picks the highest installed pack with the same
    major version as the one requested, and building against a different one
    changes inlining inside the core headers -- and with it the image size.
    """
    required = cmsis_required_version() or ''
    major = required.split('.')[0] if required else ''

    roots = [
        os.path.join(os.environ.get('LOCALAPPDATA', ''), 'Arm', 'Packs', 'ARM', 'CMSIS'),
        os.path.join(os.environ.get('APPDATA', ''), 'Arm', 'Packs', 'ARM', 'CMSIS'),
        'C:/Keil_v5/ARM/PACK/ARM/CMSIS',
    ]

    def key(version):
        return [int(part) for part in version.split('.') if part.isdigit()]

    for root in roots:
        if not os.path.isdir(root):
            continue
        versions = sorted((v for v in os.listdir(root)
                           if os.path.isdir(os.path.join(root, v))), key=key)
        if major:
            same_major = [v for v in versions if v.split('.')[0] == major]
            versions = same_major or versions
        for version in reversed(versions):
            core = os.path.join(root, version, 'CMSIS', 'Core', 'Include')
            if os.path.isfile(os.path.join(core, 'core_cm4.h')):
                return os.path.normpath(core)
    raise SystemExit('CMSIS Core headers not found; install the ARM::CMSIS pack '
                     'the project asks for')


def parse_project(target_name):
    """Return (sources, libraries, includes, defines, scatter) of a target."""
    root = ET.parse(PROJECT_FILE).getroot()

    target = None
    for tgt in root.iter('Target'):
        if tgt.findtext('TargetName') == target_name:
            target = tgt
            break
    if target is None:
        raise SystemExit('target %r not found in %s' % (target_name, PROJECT_FILE))

    files = [f.findtext('FilePath') for f in target.iter('File')]
    files = [f for f in files if f]
    sources = [to_abs(f) for f in files if f.lower().endswith('.c')]
    libraries = [to_abs(f) for f in files if f.lower().endswith('.lib')]

    ads = target.find('TargetOption/TargetArmAds')
    controls = ads.find('Cads/VariousControls')
    includes = [to_abs(p) for p in controls.findtext('IncludePath', '').split(';') if p]

    # The project writes them loosely: "A=1, B, SUPPORT_ACCESS_SHM = 1".
    defines = []
    for item in controls.findtext('Define', '').split(','):
        item = item.strip().replace(' = ', '=').replace(' ', '=')
        if item:
            defines.append(item)

    scatter = to_abs(ads.findtext('LDads/ScatterFile'))

    return sources, libraries, includes, defines, scatter


sources, libraries, includes, defines, scatter = parse_project(TARGET_NAME)

env = Environment(tools=['mingw'], ENV=os.environ)

env.Replace(
    CC=menu_config.CC,
    AS=menu_config.AS,
    AR=menu_config.AR,
    LINK=menu_config.LINK,
    FROMELF=menu_config.FROMELF,
    CCFLAGS=COMPILE_FLAGS,
    CPPDEFINES=defines,
    # mdk/ carries pre_define.h, which the project force-includes by bare name;
    # CMSIS Core is the RTE component uVision adds implicitly.
    CPPPATH=includes + [MDK_DIR, find_cmsis_core()],
    LIBPREFIX='',
    LIBSUFFIX='.lib',
)
env.PrependENVPath('PATH', menu_config.EXEC_PATH)

forced = ' '.join('--include ' + h for h in FORCED_INCLUDES)
env.Replace(CCCOM='$CC $_CCCOMCOM $CCFLAGS ' + forced + ' -c -o $TARGET $SOURCES')

# One object per source, flattened into build_scons/obj. Several modules share a
# basename, so the directory structure is folded into the object name.
objects = []
for src in sources:
    name = src.replace(BSP_DIR + os.sep, '').replace(':', '').replace(os.sep, '_')
    objects.append(env.Object(os.path.join(OUT_DIR, 'obj', name[:-2] + '.o'), src))

axf = os.path.join(OUT_DIR, 'dashboard.axf')

# --map is a switch here, not a filename: armlink writes <output>.map next to
# the image, which is what uVision does as well.
link_cmd = ('$LINK --scatter=%s --cpu=Cortex-M33 --lto '
            '--diag_suppress=L6314 --datacompressor off '
            '--info sizes --info totals --info unused --info veneers '
            '--map --output=$TARGET $SOURCES' % scatter)

image = env.Command(axf, objects + libraries, link_cmd)
env.Command(os.path.join(OUT_DIR, 'dashboard.bin'), image,
            '$FROMELF --bin --output $TARGET $SOURCE')

Clean('.', OUT_DIR)
