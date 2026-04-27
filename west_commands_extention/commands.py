#!/usr/bin/env python3
"""
RTL8773G HMI 项目的 West 扩展命令

这些命令提供了项目特定的工具和快捷方式。
"""

import os
import sys
from pathlib import Path
from west.commands import WestCommand
from west import log


class ProjectInfo(WestCommand):
    """显示项目版本和构建信息"""

    def __init__(self):
        super().__init__(
            'info',
            'show project information',
            'Display RTL8773G HMI project version and build information'
        )

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(
            self.name,
            help=self.help,
            description=self.description
        )
        return parser

    def do_run(self, args, unknown_args):
        log.inf('RTL8773G HMI Project Information')
        log.inf('=' * 50)
        log.inf('Project: RTL8773G HMI Application')
        log.inf('Owner: howie_wang')
        log.inf('Zephyr Version: realtek-main-v3.7')
        log.inf('')
        log.inf('Workspace: ' + os.getcwd())

        # 显示应用路径
        app_path = Path('zephyrproject/realtek-app/applications/hmi')
        if app_path.exists():
            log.inf(f'Application Path: {app_path}')

        log.inf('=' * 50)


class CleanAll(WestCommand):
    """清理所有构建产物和缓存"""

    def __init__(self):
        super().__init__(
            'clean-all',
            'clean all build artifacts',
            'Remove all build directories and cache files'
        )

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(
            self.name,
            help=self.help,
            description=self.description
        )
        parser.add_argument(
            '-f', '--force',
            action='store_true',
            help='force clean without confirmation'
        )
        return parser

    def do_run(self, args, unknown_args):
        import shutil

        dirs_to_clean = ['build', 'build-*']

        if not args.force:
            log.wrn('This will remove all build directories:')
            for d in dirs_to_clean:
                log.wrn(f'  - {d}')
            response = input('Continue? [y/N] ')
            if response.lower() != 'y':
                log.inf('Aborted')
                return

        log.inf('Cleaning build artifacts...')

        # 清理 build 目录
        build_dir = Path('build')
        if build_dir.exists():
            shutil.rmtree(build_dir)
            log.inf(f'Removed: {build_dir}')

        # 清理 build-* 目录
        for build_variant in Path('.').glob('build-*'):
            if build_variant.is_dir():
                shutil.rmtree(build_variant)
                log.inf(f'Removed: {build_variant}')

        log.inf('Clean completed!')


class FlashJLink(WestCommand):
    """使用 J-Link 烧录固件"""

    def __init__(self):
        super().__init__(
            'flash-jlink',
            'flash firmware using J-Link',
            'Flash the built firmware to RTL8773G using J-Link debugger'
        )

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(
            self.name,
            help=self.help,
            description=self.description
        )
        parser.add_argument(
            '--hex',
            help='path to hex file (default: build/zephyr/zephyr.hex)'
        )
        return parser

    def do_run(self, args, unknown_args):
        import subprocess

        hex_file = args.hex or 'build/zephyr/zephyr.hex'

        if not Path(hex_file).exists():
            log.die(f'Hex file not found: {hex_file}')

        log.inf(f'Flashing {hex_file} to RTL8773G using J-Link...')

        # 这里是示例，实际需要根据你的 J-Link 配置调整
        jlink_script = f'''
r
h
loadfile {hex_file}
r
go
exit
'''

        log.inf('J-Link flash command would be executed here')
        log.inf('(This is a template - customize for your hardware setup)')

        # 实际使用时取消注释：
        # with open('/tmp/jlink.cmd', 'w') as f:
        #     f.write(jlink_script)
        # subprocess.run(['JLinkExe', '-device', 'RTL8773G', '-if', 'SWD',
        #                 '-speed', '4000', '-CommandFile', '/tmp/jlink.cmd'])


class GuiDemo(WestCommand):
    """构建并运行 GUI 演示应用"""

    def __init__(self):
        super().__init__(
            'gui-demo',
            'build and run GUI demo',
            'Build the HMI application with GUI demo enabled'
        )

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(
            self.name,
            help=self.help,
            description=self.description
        )
        parser.add_argument(
            '-b', '--board',
            default='rtl8773g',
            help='target board (default: rtl8773g)'
        )
        parser.add_argument(
            '--pristine',
            action='store_true',
            help='force pristine build'
        )
        return parser

    def do_run(self, args, unknown_args):
        import subprocess

        app_path = 'zephyrproject/realtek-app/applications/hmi'

        log.inf('Building GUI demo application...')

        build_cmd = [
            'west', 'build',
            '-b', args.board,
            app_path,
            '--',
            '-DCONFIG_GUI_DEMO=y'
        ]

        if args.pristine:
            build_cmd.insert(2, '-p')

        log.inf('Command: ' + ' '.join(build_cmd))

        result = subprocess.run(build_cmd)

        if result.returncode == 0:
            log.inf('Build completed successfully!')
            log.inf('Use "west flash" to program the device')
        else:
            log.err('Build failed!')
            sys.exit(1)
