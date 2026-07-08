#!/usr/bin/env python3
"""RTL8773E Dashboard West extension commands."""

import os
import shutil
import subprocess

from west.commands import WestCommand
from west import log


def _cmake_src(manifest) -> str:
    """Return the CMake source root: the directory containing board/evb/hmi_dashboard/.

    hmi_dashboard is always structured as <cmake_src>/board/evb/hmi_dashboard/,
    so going 3 levels up from its West project abspath is environment-independent —
    works regardless of whether a wrapper project (e.g. honeycomb/) exists.
    """
    this_file = os.path.normcase(os.path.abspath(__file__))
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
        raise ValueError('Cannot locate hmi_dashboard project in manifest')

    # own_abspath ends with .../board/evb/hmi_dashboard — go 3 levels up
    return os.path.normpath(os.path.join(own_abspath, '..', '..', '..'))


def _build_root(manifest) -> str:
    # Parent directory holding per-mode build subdirectories,
    # placed alongside the cmake source root (one level up).
    return os.path.join(os.path.dirname(_cmake_src(manifest)), 'build')


def _build_dir(manifest, mode: str) -> str:
    """Per-mode build directory, e.g. build/lib_bank1.

    Each mode gets its own CMake build tree so switching modes never reuses
    a stale CMakeCache configured for a different defconfig (which used to
    silently skip re-configure and leave ninja with 'no work to do').
    """
    return os.path.join(_build_root(manifest), mode)


DEFCONFIGS = {
    'src_bank0': 'board/evb/hmi_dashboard/gcc/defconfig.RTL8773E.hmi_dashboard_src_bank0',
    'src_bank1': 'board/evb/hmi_dashboard/gcc/defconfig.RTL8773E.hmi_dashboard_src_bank1',
    'lib_bank0': 'board/evb/hmi_dashboard/gcc/defconfig.RTL8773E.hmi_dashboard_lib_bank0',
    'lib_bank1': 'board/evb/hmi_dashboard/gcc/defconfig.RTL8773E.hmi_dashboard_lib_bank1',
}

# Back-compat shorthand: old `-m src` / `-m lib` map to bank0 variants.
_MODE_ALIASES = {'src': 'src_bank0', 'lib': 'lib_bank0'}
_MODE_CHOICES = list(DEFCONFIGS.keys()) + list(_MODE_ALIASES.keys())


def _resolve_mode(mode: str) -> str:
    """Apply alias map; return the canonical mode name used to index DEFCONFIGS."""
    return _MODE_ALIASES.get(mode, mode)


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
        cmake_src = _cmake_src(self.manifest)
        build_root = _build_root(self.manifest)

        built_modes = []
        if os.path.isdir(build_root):
            for name in sorted(os.listdir(build_root)):
                if name in DEFCONFIGS and os.path.exists(
                        os.path.join(build_root, name, 'CMakeCache.txt')):
                    built_modes.append(name)
        built = bool(built_modes)

        log.inf('RTL8773E Dashboard Project')
        log.inf('=' * 54)
        log.inf(f'  Workspace : {topdir}')
        log.inf(f'  SDK root  : {cmake_src}')
        log.inf(f'  Build root: {build_root}')
        log.inf(f'  Built     : {", ".join(built_modes) if built else "no — run: west build"}')

        if built:
            bin_root = os.path.join(cmake_src, 'board', 'evb', 'hmi_dashboard', 'gcc', 'bin')
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
            '-m', '--mode', choices=_MODE_CHOICES, default='src_bank0',
            help=('build target: src_bank0/src_bank1/lib_bank0/lib_bank1; '
                  'src and lib are shorthand for src_bank0 / lib_bank0 '
                  '(default: src_bank0)')
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
        cmake_src = _cmake_src(self.manifest)
        mode = _resolve_mode(args.mode)
        build_dir = _build_dir(self.manifest, mode)
        defconfig = DEFCONFIGS[mode]

        if args.clean and os.path.exists(build_dir):
            log.inf(f'Cleaning: {build_dir}')
            shutil.rmtree(build_dir)

        already_configured = os.path.exists(os.path.join(build_dir, 'CMakeCache.txt'))
        if not already_configured or args.configure_only:
            cfg_cmd = ['cmake', '-G', 'Ninja', '-S', cmake_src,
                       '-D', f'kconfig_path={defconfig}',
                       '-DIS_CHECK_FLOW=OFF', '-Dcompile_lib_only=OFF', '-B', build_dir]
            log.inf('Configuring...')
            log.dbg(' '.join(cfg_cmd))
            r = subprocess.run(cfg_cmd, cwd=cmake_src)
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
        r = subprocess.run(build_cmd, cwd=cmake_src)
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
            '-m', '--mode', choices=_MODE_CHOICES, default=None,
            help='only remove this mode\'s build dir (default: remove all modes)'
        )
        parser.add_argument(
            '--all', action='store_true',
            help='also remove bin/ output directories under board/evb/hmi_dashboard/'
        )
        return parser

    def do_run(self, args, unknown_args):
        cmake_src = _cmake_src(self.manifest)

        if args.mode:
            target = _build_dir(self.manifest, _resolve_mode(args.mode))
        else:
            target = _build_root(self.manifest)

        if os.path.exists(target):
            log.inf(f'Removing {target}')
            shutil.rmtree(target)
            log.inf('Done.')
        else:
            log.inf('Nothing to clean (build directory does not exist).')

        if args.all:
            bin_dir = os.path.join(cmake_src, 'board', 'evb', 'hmi_dashboard', 'bin')
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
            '-m', '--mode', choices=_MODE_CHOICES, default='src_bank0',
            help='which build to flash (default: src_bank0)'
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

        cmake_src = _cmake_src(self.manifest)
        download_bat = os.path.join(
            cmake_src, 'board', 'evb', 'hmi_dashboard', 'gcc', 'download.bat'
        )
        if not os.path.exists(download_bat):
            log.die(f'download.bat not found: {download_bat}')

        # download.bat now takes [COM] [MODE] [USERDATA_FILE USERDATA_ADDR]
        # — both COM and MODE are positional, supply defaults if not given.
        mode = _resolve_mode(args.mode)
        port = args.port if args.port else 'COM3'
        cmd = ['cmd', '/c', download_bat, port, mode]
        if args.userdata:
            cmd += [args.userdata, args.userdata_addr]

        subprocess.run(cmd)


# Both toolchains share the same bulidRTL8773E.bat / install layout
# (lib/armclang and lib/arm-none-eabi-gcc are parallel directory structures),
# just with a different compiler and static-lib output name.
_GUI_TOOLCHAINS = [
    {'name': 'armclang', 'src_dir': 'armclang', 'lib_name': 'gui.lib', 'dst_subdir': 'armclang'},
    {'name': 'arm-none-eabi-gcc', 'src_dir': 'arm-none-eabi-gcc', 'lib_name': 'libgui.a', 'dst_subdir': 'gcc'},
]


class GuiLibCommand(WestCommand):
    def __init__(self):
        super().__init__(
            'guilib', 'build and sync the HoneyGUI static libraries',
            'Build the HoneyGUI armclang (MDK) and arm-none-eabi-gcc (GCC) '
            'static libraries from source, then copy the resulting libs and '
            'headers into board/evb/hmi_dashboard/src/gui_lib/'
        )

    def do_add_parser(self, parser_adder, **kwargs):
        return parser_adder.add_parser(self.name, help=self.help,
                                       description=self.description)

    def do_run(self, args, unknown_args):
        # cmake_src is the sdk root (computed relative to hmi_dashboard's own
        # abspath), so it transparently absorbs the extra honeycomb/ wrapper
        # directory that only exists in this dev workspace.
        cmake_src = _cmake_src(self.manifest)
        gui_lib_root = os.path.join(cmake_src, 'src', 'sample', 'gui', 'lib')
        gui_lib_dir = os.path.join(cmake_src, 'board', 'evb', 'hmi_dashboard', 'src', 'gui_lib')

        include_src = None
        for tc in _GUI_TOOLCHAINS:
            toolchain_dir = os.path.join(gui_lib_root, tc['src_dir'])
            build_bat = os.path.join(toolchain_dir, 'bulidRTL8773E.bat')
            if not os.path.exists(build_bat):
                log.die(f'Build script not found: {build_bat}')

            log.inf(f'[{tc["name"]}] Running {build_bat} ...')
            # bulidRTL8773E.bat ends with `pause`; feed it a newline on stdin
            # so it doesn't hang waiting for a keypress.
            r = subprocess.run(['cmd', '/c', build_bat], cwd=toolchain_dir,
                               input='\n', text=True)
            if r.returncode != 0:
                log.die(f'[{tc["name"]}] HoneyGUI build failed')

            # NOTE: install prefix is a sibling of temp/, not nested inside
            # it -- the bat's `cmake -B ./temp` omits -S, so the source dir
            # (and thus CMAKE_INSTALL_PREFIX) resolves to toolchain_dir itself.
            install_dir = os.path.join(toolchain_dir, 'install')
            lib_src = os.path.join(install_dir, 'lib', tc['lib_name'])
            tc_include_src = os.path.join(install_dir, 'include')
            if not os.path.exists(lib_src):
                log.die(f'[{tc["name"]}] Build did not produce {lib_src}')
            if not os.path.isdir(tc_include_src):
                log.die(f'[{tc["name"]}] Build did not produce {tc_include_src}')

            dst_dir = os.path.join(gui_lib_dir, tc['dst_subdir'])
            log.inf(f'[{tc["name"]}] Clearing {dst_dir}')
            shutil.rmtree(dst_dir, ignore_errors=True)
            os.makedirs(dst_dir)

            log.inf(f'[{tc["name"]}] Copying {lib_src} -> {dst_dir}')
            shutil.copy2(lib_src, dst_dir)

            # Headers are toolchain-independent (same .config on both sides);
            # keep the last build's output to sync into the shared include/ dir.
            include_src = tc_include_src

        include_dst = os.path.join(gui_lib_dir, 'include')
        log.inf(f'Clearing {include_dst}')
        shutil.rmtree(include_dst, ignore_errors=True)

        log.inf(f'Copying {include_src} -> {include_dst}')
        shutil.copytree(include_src, include_dst)

        log.inf('Done.')


class SyncCommand(WestCommand):
    def __init__(self):
        super().__init__(
            'sync', 'update all repos and submodules',
            'Force-update the manifest repo, run west update, then '
            'git submodule update --init --recursive for every project '
            'that contains a .gitmodules file'
        )

    def do_add_parser(self, parser_adder, **kwargs):
        return parser_adder.add_parser(self.name, help=self.help,
                                       description=self.description)

    def do_run(self, args, unknown_args):
        topdir = self.manifest.topdir

        # Step 0: force-update the manifest repo itself
        manifest_dir = self.manifest.projects[0].abspath
        log.inf(f'Fetching manifest repo: {manifest_dir}')
        r = subprocess.run(['git', 'fetch', 'origin'], cwd=manifest_dir)
        if r.returncode != 0:
            log.wrn('git fetch on manifest repo failed, skipping reset')
        else:
            res = subprocess.run(
                ['git', 'branch', '--show-current'],
                cwd=manifest_dir, capture_output=True, text=True,
            )
            branch = res.stdout.strip()
            if branch:
                r = subprocess.run(
                    ['git', 'reset', '--hard', f'origin/{branch}'],
                    cwd=manifest_dir,
                )
                if r.returncode != 0:
                    log.wrn('git reset --hard on manifest repo failed')
            else:
                log.wrn('Manifest repo is in detached HEAD state, skipping reset')

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

    def _find_elfs(self, cmake_src: str):
        bin_root = os.path.join(cmake_src, 'board', 'evb', 'hmi_dashboard', 'gcc', 'bin')
        elfs = []
        if not os.path.exists(bin_root):
            return elfs
        for root, _dirs, files in os.walk(bin_root):
            for f in files:
                if f.endswith('.elf'):
                    elfs.append(os.path.join(root, f))
        return elfs

    def do_run(self, args, unknown_args):
        cmake_src = _cmake_src(self.manifest)
        elfs = self._find_elfs(cmake_src)

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
