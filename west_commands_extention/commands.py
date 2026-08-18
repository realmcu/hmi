#!/usr/bin/env python3
"""RTL8773E Dashboard West extension commands."""

import os
import re
import shutil
import subprocess
import tempfile

from west.commands import WestCommand
from west import log


# Default serial port for all west download commands (flash/userdata).
# The only place to edit the west default; override with `-p COMx`.
DEFAULT_COM = 'COM3'


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


IC_TYPE = '8773E'


def _designer_romfs_bin(cmake_src: str) -> str:
    return os.path.join(cmake_src, 'board', 'evb', 'hmi_dashboard', 'src',
                         'application', 'designer', 'build', 'app_romfs.bin')


def _flash_map_h(cmake_src: str) -> str:
    return os.path.join(cmake_src, 'bin', 'rtl87x3ep', 'flash_map_config',
                         '16M', 'flash_16M', 'flash_map.h')


def _parse_flash_map_define(cmake_src: str, name: str) -> str:
    """Read a `#define NAME 0x...` value out of the SDK's flash_map.h."""
    flash_map = _flash_map_h(cmake_src)
    if not os.path.exists(flash_map):
        log.die(f'flash_map.h not found: {flash_map}')
    with open(flash_map, 'r') as f:
        text = f.read()
    m = re.search(rf'#define\s+{re.escape(name)}\s+(0x[0-9A-Fa-f]+)', text)
    if not m:
        log.die(f'Cannot find {name} in {flash_map}')
    return m.group(1)


def _gadgets_dir(cmake_src: str) -> str:
    return os.path.join(cmake_src, 'tool', 'Gadgets')


def _mpcli_flash(cmake_src: str, port: str, bin_path: str, addr: str):
    """Flash bin_path to addr directly via mpcli, bypassing gcc/download.bat
    (which always insists on resolving and flashing an app image first).
    """
    mpcli_dir = os.path.join(cmake_src, 'board', 'evb', 'hmi_dashboard',
                             'download', 'mpcli')
    mpcli_exe = os.path.join(mpcli_dir, 'mpcli.exe')
    if not os.path.exists(mpcli_exe):
        log.die(f'mpcli.exe not found: {mpcli_exe}')

    cmd = [mpcli_exe, '-c', port, '-p', '-A', addr, '-F', bin_path,
           '-b', '3000000', '-M', '5', '-r', '-u', '-d', '-T', 'RTL87X3EP']
    log.dbg(' '.join(cmd))
    r = subprocess.run(cmd, cwd=mpcli_dir)
    if r.returncode != 0:
        log.die('userdata flash failed')


def _package_userdata(cmake_src: str, bin_path: str):
    """Prepend a real RTL8773E user_data1 header to bin_path.

    Returns (record_bin, flash_ready_bin, workdir):

    - record_bin: the full official MP-tagged image — 1024-byte ctrl
      header + 512-byte MP production header (BinID/Version/PartNumber
      from mp_data1.ini) + payload — copied next to bin_path for
      production traceability. Mirrors
      tool/Gadgets/gui_package_tool/8773E/gen_root_image.bat.
    - flash_ready_bin: the same image with the leading 512-byte MP
      production header stripped back off, leaving just [1024B ctrl
      header][payload]. mpcli does a raw byte-for-byte flash write with
      no header-aware stripping (unlike e.g. MPPGTool), so record_bin
      would misalign the mount address by 0x200 bytes if flashed
      directly — flash_ready_bin is what must actually be written at
      USER_DATA1_ADDR for the payload to land at the +0x400 offset
      gui_vfs_mount_romfs() expects. Lives inside workdir.
    - workdir: scratch temp dir backing flash_ready_bin; caller must
      shutil.rmtree() it once done flashing.

    bin_path itself is never modified — designer/'s committed
    app_romfs.bin must stay header-free.
    """
    gadgets = _gadgets_dir(cmake_src)
    prepend_header = os.path.join(gadgets, 'prepend_header.exe')
    md5_tool = os.path.join(gadgets, 'md5.exe')
    mp_ini = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                          '..', 'download', 'mp_data1.ini')
    for tool in (prepend_header, md5_tool, mp_ini):
        if not os.path.exists(tool):
            log.die(f'Required tool/config not found: {tool}')

    workdir = tempfile.mkdtemp(prefix='hmi_userdata_')
    basename = os.path.basename(bin_path)
    work_bin = os.path.join(workdir, basename)
    shutil.copy2(bin_path, work_bin)

    r = subprocess.run([prepend_header, '/user_data1', work_bin,
                        '/ic_type', IC_TYPE], cwd=workdir)
    if r.returncode != 0:
        log.die('prepend_header (raw header) failed')

    # work_bin now holds [1024B ctrl header][payload] — exactly what
    # needs to land on flash. Snapshot it before the next step, which
    # creates a new *_MP.bin file rather than touching work_bin.
    flash_ready = os.path.join(workdir, 'flash_ready.bin')
    shutil.copy2(work_bin, flash_ready)

    r = subprocess.run([prepend_header, '/user_data1', work_bin,
                        '/mp_ini', mp_ini, '/ic_type', IC_TYPE], cwd=workdir)
    if r.returncode != 0:
        log.die('prepend_header (mp_ini) failed')

    stem, ext = os.path.splitext(basename)
    mp_bin = os.path.join(workdir, f'{stem}_MP{ext}')
    if not os.path.exists(mp_bin):
        log.die(f'prepend_header did not produce {mp_bin}')

    r = subprocess.run([md5_tool, mp_bin], cwd=workdir,
                       capture_output=True, text=True)
    if r.returncode != 0:
        log.die('md5 tagging failed')
    m = re.search(r'Output Image:\s*(\S+)', r.stdout)
    if not m:
        log.die(f'Could not parse md5 tool output: {r.stdout!r}')

    record_name = os.path.basename(m.group(1))
    record_dst = os.path.join(os.path.dirname(bin_path), record_name)
    shutil.copy2(os.path.join(workdir, record_name), record_dst)

    return record_dst, flash_ready, workdir


class _Repository:
    def __init__(self, name, path, abspath, revision='-'):
        self.name = name
        self.path = path
        self.abspath = abspath
        self.revision = revision


class _RepositoryState:
    def __init__(self, name, path, revision, commit, status, changes,
                 error=''):
        self.name = name
        self.path = path
        self.revision = revision
        self.commit = commit
        self.status = status
        self.changes = changes
        self.error = error


def _git(repo_dir, *args):
    """Run git without invoking a shell and return its decoded output."""
    return subprocess.run(
        ['git', *args], cwd=repo_dir, capture_output=True, text=True,
        encoding='utf-8', errors='replace',
    )


def _gitmodule_paths(repo_dir):
    """Return paths declared by this repository's .gitmodules file."""
    if not os.path.isfile(os.path.join(repo_dir, '.gitmodules')):
        return []
    result = _git(
        repo_dir, 'config', '-f', '.gitmodules',
        '--get-regexp', r'^submodule\..*\.path$',
    )
    if result.returncode not in (0, 1):
        return []
    return [
        line.split(None, 1)[1].replace('\\', '/').rstrip('/')
        for line in result.stdout.splitlines() if len(line.split(None, 1)) == 2
    ]


def _project_state(project, projects, show_files):
    """Collect the current commit and worktree state of one repository."""
    repo_dir = project.abspath
    path = project.path or '.'
    if not os.path.isdir(repo_dir):
        return _RepositoryState(
            project.name, path, project.revision, '-', 'MISSING', [],
            'project directory does not exist',
        )
    if not os.path.exists(os.path.join(repo_dir, '.git')):
        return _RepositoryState(
            project.name, path, project.revision, '-', 'UNINIT', [],
            'repository is not initialized',
        )

    head = _git(repo_dir, 'rev-parse', '--verify', 'HEAD')
    if head.returncode != 0:
        error = head.stderr.strip() or 'not a Git repository'
        return _RepositoryState(
            project.name, path, project.revision, '-', 'ERROR', [], error,
        )

    untracked = 'all' if show_files else 'normal'
    status_args = ['status', '--porcelain=v1', f'--untracked-files={untracked}']
    nested_paths = []
    gitmodule_paths = set(_gitmodule_paths(repo_dir))
    repo_dir_abs = os.path.abspath(repo_dir)
    repo_dir_norm = os.path.normcase(repo_dir_abs)
    for nested in projects:
        if nested is project:
            continue
        nested_dir_abs = os.path.abspath(nested.abspath)
        nested_dir_norm = os.path.normcase(nested_dir_abs)
        try:
            common = os.path.commonpath([repo_dir_norm, nested_dir_norm])
        except ValueError:
            continue
        if common != repo_dir_norm:
            continue
        relative = os.path.relpath(nested_dir_abs, repo_dir_abs)
        relative = relative.replace(os.sep, '/')
        if (relative != '.' and not relative.startswith('../') and
                relative not in gitmodule_paths):
            nested_paths.append(relative)

    if nested_paths:
        status_args.append('--')
        status_args.append('.')
        for nested_path in nested_paths:
            status_args.extend((
                f':(exclude){nested_path}', f':(exclude){nested_path}/',
            ))

    worktree = _git(repo_dir, *status_args)
    if worktree.returncode != 0:
        error = worktree.stderr.strip() or 'git status failed'
        return _RepositoryState(
            project.name, path, project.revision, head.stdout.strip(),
            'ERROR', [], error,
        )

    changes = worktree.stdout.splitlines()
    return _RepositoryState(
        project.name, path, project.revision, head.stdout.strip(),
        'DIRTY' if changes else 'CLEAN', changes if show_files else [],
    )


def _collect_repository_states(manifest, show_files=False):
    projects = list(manifest.projects)
    repositories = [
        _Repository(project.name, project.path or '.', project.abspath,
                    project.revision)
        for project in projects
    ]

    west_paths = {
        os.path.normcase(os.path.abspath(repository.abspath))
        for repository in repositories
    }
    submodule_index = 0
    while submodule_index < len(repositories):
        parent = repositories[submodule_index]
        submodule_index += 1
        for submodule_path in _gitmodule_paths(parent.abspath):
            abspath = os.path.abspath(os.path.join(parent.abspath, submodule_path))
            normalized = os.path.normcase(abspath)
            if normalized in west_paths:
                continue
            workspace_path = os.path.relpath(abspath, manifest.topdir).replace(os.sep, '/')
            repositories.append(_Repository(
                f'submodule:{workspace_path}', workspace_path, abspath,
            ))
            west_paths.add(normalized)

    return [
        _project_state(repository, repositories, show_files)
        for repository in repositories
    ]


class ProjectInfo(WestCommand):
    def __init__(self):
        super().__init__(
            'info', 'show project information',
            'Display RTL8773E Dashboard workspace and build status'
        )

    def do_add_parser(self, parser_adder, **kwargs):
        parser = parser_adder.add_parser(
            self.name, help=self.help, description=self.description,
        )
        parser.add_argument(
            '-f', '--files', action='store_true',
            help='list changed and untracked files in dirty repositories',
        )
        return parser

    def do_run(self, args, unknown):
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
        log.inf('=' * 96)
        log.inf(f'  Workspace : {topdir}')
        log.inf(f'  SDK root  : {cmake_src}')
        log.inf(f'  Build root: {build_root}')
        log.inf(f'  Built     : {", ".join(built_modes) if built else "no — run: west build"}')
        log.inf('')
        log.inf('Repositories')
        log.inf('-' * 96)
        log.inf(f'{"Status":<8} {"Project":<24} {"Commit":<40} Path')
        log.inf('-' * 96)

        states = _collect_repository_states(self.manifest, args.files)
        for state in states:
            commit = state.commit if state.commit == '-' else state.commit[:40]
            log.inf(f'{state.status:<8} {state.name:<24} {commit:<40} {state.path}')
            if state.error:
                log.inf(f'         ! {state.error}')
            for change in state.changes:
                log.inf(f'         {change}')

        clean = sum(state.status == 'CLEAN' for state in states)
        dirty = sum(state.status == 'DIRTY' for state in states)
        errors = len(states) - clean - dirty
        log.inf('-' * 96)
        summary = f'Total: {len(states)}, clean: {clean}, dirty: {dirty}'
        if errors:
            summary += f', unavailable: {errors}'
        log.inf(summary)

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

        log.inf('=' * 96)


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

    def do_run(self, args, unknown):
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

    def do_run(self, args, unknown):
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
            help=f'serial COM port (default: {DEFAULT_COM})'
        )
        parser.add_argument(
            '-m', '--mode', choices=_MODE_CHOICES, default='src_bank0',
            help='which build to flash (default: src_bank0)'
        )
        return parser

    def do_run(self, args, unknown):
        cmake_src = _cmake_src(self.manifest)
        download_bat = os.path.join(
            cmake_src, 'board', 'evb', 'hmi_dashboard', 'gcc', 'download.bat'
        )
        if not os.path.exists(download_bat):
            log.die(f'download.bat not found: {download_bat}')

        mode = _resolve_mode(args.mode)
        port = args.port if args.port else DEFAULT_COM
        cmd = ['cmd', '/c', download_bat, port, mode]
        subprocess.run(cmd)


class UserdataCommand(WestCommand):
    def __init__(self):
        super().__init__(
            'userdata', 'package and flash a userdata (user_data1) binary',
            'Prepend a real RTL8773E MP header to a userdata binary and '
            'flash it standalone via mpcli — the app image is never '
            'touched (use `west flash` for that). Defaults to the '
            'designer UI\'s src/application/designer/build/app_romfs.bin.'
        )

    def do_add_parser(self, parser_adder, **kwargs):
        parser = parser_adder.add_parser(self.name, help=self.help,
                                         description=self.description)
        parser.add_argument(
            'file', metavar='FILE', nargs='?', default=None,
            help=('userdata binary to package/flash (default: the '
                  'designer UI\'s src/application/designer/build/app_romfs.bin). '
                  'Source file is never modified.')
        )
        parser.add_argument(
            '-p', '--port', default='',
            help=f'serial COM port (default: {DEFAULT_COM})'
        )
        parser.add_argument(
            '--addr', metavar='ADDR',
            help='flash address (default: USER_DATA1_ADDR from the SDK flash_map.h)'
        )
        parser.add_argument(
            '--package-only', action='store_true',
            help=('only add the MP header and write the record bin next '
                  'to the source file — do not touch the serial port at all')
        )
        return parser

    def do_run(self, args, unknown):
        cmake_src = _cmake_src(self.manifest)
        userdata_bin = args.file or _designer_romfs_bin(cmake_src)
        if not os.path.exists(userdata_bin):
            log.die(f'userdata bin not found: {userdata_bin}')

        log.inf(f'Packaging userdata: {userdata_bin}')
        record, flash_ready, workdir = _package_userdata(cmake_src, userdata_bin)
        log.inf(f'Record (MP-tagged, for reference) -> {record}')
        try:
            if args.package_only:
                return
            addr = args.addr or _parse_flash_map_define(cmake_src, 'USER_DATA1_ADDR')
            port = args.port if args.port else DEFAULT_COM
            log.inf(f'Flashing userdata -> {addr} (app is not touched)')
            _mpcli_flash(cmake_src, port, flash_ready, addr)
        finally:
            shutil.rmtree(workdir, ignore_errors=True)


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

    def do_run(self, args, unknown):
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

    def do_run(self, args, unknown):
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
        cmd = ['west', 'update'] + list(unknown)
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

    def do_run(self, args, unknown):
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
