#!/usr/bin/env python3
"""RTL8773E Dashboard West extension commands."""

import os
import shutil
import subprocess

from west.commands import WestCommand
from west import log


def _sdk_root(manifest) -> str:
    this_file = os.path.normcase(os.path.abspath(__file__))

    # Find the manifest project that directly contains this file
    own_abspath = None
    for project in manifest.projects:
        try:
            abspath = os.path.normcase(os.path.abspath(project.abspath))
        except Exception:
            continue
        if this_file.startswith(abspath + os.sep) and (
                own_abspath is None or len(abspath) > len(own_abspath)):
            own_abspath = abspath

    if own_abspath is None:
        raise ValueError('Cannot locate own project in manifest')

    # Find the project whose abspath is a proper ancestor of own_abspath
    best_path = None
    best_len = -1
    for project in manifest.projects:
        try:
            abspath = os.path.normcase(os.path.abspath(project.abspath))
        except Exception:
            continue
        if abspath != own_abspath and own_abspath.startswith(abspath + os.sep):
            if len(abspath) > best_len:
                best_path = project.abspath
                best_len = len(abspath)

    if best_path is None:
        raise ValueError('Cannot find SDK project (ancestor of hmi_dashboard) in manifest')
    return best_path


def _build_dir(manifest) -> str:
    return os.path.join(_sdk_root(manifest), 'build')


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
        sdk_root = _sdk_root(self.manifest)
        build_dir = _build_dir(self.manifest)
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
        sdk_root = _sdk_root(self.manifest)
        build_dir = _build_dir(self.manifest)
        defconfig = DEFCONFIGS[args.mode]

        if args.clean and os.path.exists(build_dir):
            log.inf(f'Cleaning: {build_dir}')
            shutil.rmtree(build_dir)

        already_configured = os.path.exists(os.path.join(build_dir, 'CMakeCache.txt'))
        if not already_configured or args.configure_only:
            cfg_cmd = ['cmake', '-G', 'Ninja', '-D', f'kconfig_path={defconfig}',
                       '-DIS_CHECK_FLOW=OFF', '-Dcompile_lib_only=OFF', '-B', build_dir]
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
        sdk_root = _sdk_root(self.manifest)
        build_dir = _build_dir(self.manifest)

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

        sdk_root = _sdk_root(self.manifest)
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


class SyncCommand(WestCommand):
    def __init__(self):
        super().__init__(
            'sync', 'update all repos and submodules',
            'Run west update then git submodule update --init --recursive '
            'for every project that contains a .gitmodules file'
        )

    def do_add_parser(self, parser_adder, **kwargs):
        return parser_adder.add_parser(self.name, help=self.help,
                                       description=self.description)

    def do_run(self, args, unknown_args):
        topdir = self.manifest.topdir

        # Step 1: delegate to the real west update, forwarding any extra flags
        cmd = ['west', 'update'] + list(unknown_args)
        log.inf('Running: ' + ' '.join(cmd))
        r = subprocess.run(cmd, cwd=topdir)
        if r.returncode != 0:
            log.die('west update failed')

        # Step 2: for each manifest project that ships submodules, update them
        updated = []
        for project in self.manifest.projects:
            proj_dir = os.path.join(topdir, project.path)
            if not os.path.isdir(proj_dir):
                continue
            if not os.path.isfile(os.path.join(proj_dir, '.gitmodules')):
                continue
            log.inf(f'Updating submodules in {project.path} ...')
            r = subprocess.run(
                ['git', 'submodule', 'update', '--init', '--recursive'],
                cwd=proj_dir,
            )
            if r.returncode != 0:
                log.wrn(f'git submodule update failed in {project.path}')
            else:
                updated.append(project.path)

        if updated:
            log.inf('Submodules updated in: ' + ', '.join(updated))
        log.inf('Sync complete.')


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
        sdk_root = _sdk_root(self.manifest)
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
