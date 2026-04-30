#!/usr/bin/env python3
"""
RTL8773E HMI 项目的 West 扩展命令
"""

import os
from west.commands import WestCommand
from west import log


class ProjectInfo(WestCommand):
    """显示项目信息"""

    def __init__(self):
        super().__init__(
            'info',
            'show project information',
            'Display RTL8773E HMI project information'
        )

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(
            self.name,
            help=self.help,
            description=self.description
        )
        return parser

    def do_run(self, args, unknown_args):
        log.inf('RTL8773E HMI Project Information')
        log.inf('=' * 50)
        log.inf('Project: RTL8773E HMI Application')
        log.inf('')
        log.inf('Workspace: ' + os.getcwd())
        log.inf('=' * 50)
