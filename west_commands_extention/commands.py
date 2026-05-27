#!/usr/bin/env python3
"""RTL8773E Dashboard West extension commands."""

import os
import shutil
import subprocess

from west.commands import WestCommand
from west import log


def _sdk_root(topdir: str) -> str:
    return os.path.join(topdir, 'honeycomb', 'sdk')


def _build_dir(topdir: str) -> str:
    return os.path.join(_sdk_root(topdir), 'build')


DEFCONFIGS = {
    'src': 'board/evb/hmi_dashboard/gcc/defconfig.RTL8773E.hmi_dashboard_src',
    'lib': 'board/evb/hmi_dashboard/gcc/defconfig.RTL8773E.hmi_dashboard_lib',
}


class ProjectInfo(WestCommand):
    def __init__(self):
        super().__init__(
            'info', 'show project information',
            'Display RTL8773E Dashboard workspace and build status'
        )

    def do_add_parser(self, parser_adder, **kwargs):
        return parser_adder.add_parser(self.name, help=self.help,
                                       description=self.description)

    def do_run(self, args, unknown_args):
        topdir = self.manifest.topdir
        sdk_root = _sdk_root(topdir)
        build_dir = _build_dir(topdir)
        built = os.path.exists(build_dir)

        log.inf('RTL8773E Dashboard Project')
        log.inf('=' * 54)
        log.inf(f'  Workspace : {topdir}')
        log.inf(f'  SDK root  : {sdk_root}')
        log.inf(f'  Build dir : {build_dir}')
        log.inf(f'  Built     : {"yes" if built else "no — run: west build"}')

        if built:
            bin_root = os.path.join(sdk_root, 'board', 'evb', 'hmi_dashboard', 'bin')
            if os.path.exists(bin_root):
                elfs = []
                for root, _dirs, files in os.walk(bin_root):
                    for f in files:
                        if f.endswith('.elf'):
                            elfs.append(os.path.join(root, f))
                if elfs:
                    log.inf('')
                    log.inf('  Output ELFs:')
                    for elf in elfs:
                        log.inf(f'    {elf}')

        version_h = os.path.join(sdk_root, 'board', 'evb', 'hmi_dashboard', 'version.h')
        if os.path.exists(version_h):
            log.inf('')
            log.inf('  Version:')
            with open(version_h) as f:
                for line in f:
                    line = line.strip()
                    if line.startswith('#define') and 'VERSION' in line:
                        log.inf(f'    {line}')

        log.inf('=' * 54)


class BuildCommand(WestCommand):
    def __init__(self):
        super().__init__(
            'build', 'build the firmware',
            'Configure and build the RTL8773E Dashboard firmware (CMake + Ninja)'
        )

    def do_add_parser(self, parser_adder, **kwargs):
        parser = parser_adder.add_parser(self.name, help=self.help,
                                         description=self.description)
        parser.add_argument(
            '-m', '--mode', choices=['src', 'lib'], default='src',
            help='src=build from source, lib=use precompiled libgui.a (default: src)'
        )
        parser.add_argument(
            '-c', '--clean', action='store_true',
            help='remove build directory before configuring'
        )
        parser.add_argument(
            '-j', '--jobs', type=int, metavar='N',
            help='number of parallel build jobs'
        )
        parser.add_argument(
            '--configure-only', action='store_true',
            help='run cmake configure step only, skip build'
        )
        return parser

    def do_run(self, args, unknown_args):
        topdir = self.manifest.topdir
        sdk_root = _sdk_root(topdir)
        build_dir = _build_dir(topdir)
        defconfig = DEFCONFIGS[args.mode]

        if args.clean and os.path.exists(build_dir):
            log.inf(f'Cleaning: {build_dir}')
            shutil.rmtree(build_dir)

        already_configured = os.path.exists(os.path.join(build_dir, 'CMakeCache.txt'))
        if not already_configured or args.configure_only:
            cfg_cmd = ['cmake', '-G', 'Ninja', f'-Dkconfig_path={defconfig}',
                       '-DIS_CHECK_FLOW=OFF', '-B', build_dir]
            log.inf('Configuring...')
            log.dbg(' '.join(cfg_cmd))
            r = subprocess.run(cfg_cmd, cwd=sdk_root)
            if r.returncode != 0:
                log.die('CMake configure failed')

        if args.configure_only:
            log.inf('Configure done (--configure-only, skipping build).')
            return

        build_cmd = ['cmake', '--build', build_dir]
        if args.jobs:
            build_cmd += ['--parallel', str(args.jobs)]
        log.inf('Building...')
        log.dbg(' '.join(build_cmd))
        r = subprocess.run(build_cmd, cwd=sdk_root)
        if r.returncode != 0:
            log.die('Build failed')

        log.inf('Done.')


class CleanCommand(WestCommand):
    def __init__(self):
        super().__init__(
            'clean', 'clean build artifacts',
            'Remove the CMake build directory'
        )

    def do_add_parser(self, parser_adder, **kwargs):
        parser = parser_adder.add_parser(self.name, help=self.help,
                                         description=self.description)
        parser.add_argument(
            '--all', action='store_true',
            help='also remove bin/ output directories under board/evb/hmi_dashboard/'
        )
        return parser

    def do_run(self, args, unknown_args):
        topdir = self.manifest.topdir
        sdk_root = _sdk_root(topdir)
        build_dir = _build_dir(topdir)

        if os.path.exists(build_dir):
            log.inf(f'Removing {build_dir}')
            shutil.rmtree(build_dir)
            log.inf('Done.')
        else:
            log.inf('Nothing to clean (build directory does not exist).')

        if args.all:
            bin_dir = os.path.join(sdk_root, 'board', 'evb', 'hmi_dashboard', 'bin')
            if os.path.exists(bin_dir):
                log.inf(f'Removing {bin_dir}')
                shutil.rmtree(bin_dir)
                log.inf('Bin directory removed.')
            else:
                log.inf('Bin directory does not exist.')


class FlashCommand(WestCommand):
    def __init__(self):
        super().__init__(
            'flash', 'flash firmware to device',
            'Download the built MP binary to RTL8773E via serial port (calls gcc/download.bat)'
        )

    def do_add_parser(self, parser_adder, **kwargs):
        parser = parser_adder.add_parser(self.name, help=self.help,
                                         description=self.description)
        parser.add_argument(
            '-p', '--port', default='',
            help='serial COM port (default: COM3, as defined in download.bat)'
        )
        parser.add_argument(
            '--userdata', metavar='FILE',
            help='optional userdata binary to flash'
        )
        parser.add_argument(
            '--userdata-addr', metavar='ADDR',
            help='flash address for userdata (required when --userdata is given)'
        )
        return parser

    def do_run(self, args, unknown_args):
        if args.userdata and not args.userdata_addr:
            log.die('--userdata-addr is required when --userdata is given')

        topdir = self.manifest.topdir
        sdk_root = _sdk_root(topdir)
        download_bat = os.path.join(
            sdk_root, 'board', 'evb', 'hmi_dashboard', 'gcc', 'download.bat'
        )
        if not os.path.exists(download_bat):
            log.die(f'download.bat not found: {download_bat}')

        cmd = ['cmd', '/c', download_bat]
        if args.port:
            cmd.append(args.port)
        if args.userdata:
            cmd += [args.userdata, args.userdata_addr]

        subprocess.run(cmd)


class SizeCommand(WestCommand):
    def __init__(self):
        super().__init__(
            'size', 'show firmware memory usage',
            'Display code/data/bss section sizes via arm-none-eabi-size'
        )

    def do_add_parser(self, parser_adder, **kwargs):
        return parser_adder.add_parser(self.name, help=self.help,
                                       description=self.description)

    def _find_elfs(self, sdk_root: str):
        bin_root = os.path.join(sdk_root, 'board', 'evb', 'hmi_dashboard', 'gcc', 'bin')
        elfs = []
        if not os.path.exists(bin_root):
            return elfs
        for root, _dirs, files in os.walk(bin_root):
            for f in files:
                if f.startswith('honeygui_') and f.endswith('.elf'):
                    elfs.append(os.path.join(root, f))
        return elfs

    def do_run(self, args, unknown_args):
        topdir = self.manifest.topdir
        sdk_root = _sdk_root(topdir)
        elfs = self._find_elfs(sdk_root)

        if not elfs:
            log.die('No ELF found. Run: west build')

        for elf in elfs:
            log.inf(f'ELF: {elf}')
            r = subprocess.run(['arm-none-eabi-size', '-A', elf],
                               capture_output=True, text=True)
            if r.returncode != 0:
                log.wrn(f'arm-none-eabi-size failed: {r.stderr.strip()}')
                continue

            log.inf(r.stdout)

            mp_bin = elf.replace('.elf', '_MP.bin')
            if os.path.exists(mp_bin):
                sz = os.path.getsize(mp_bin)
                log.inf(f'MP binary : {sz:,} bytes  ({sz / 1024:.1f} KB)')
