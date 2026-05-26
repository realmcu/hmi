# SPDX-License-Identifier: Apache-2.0
"""West extension commands for the HMI / RustMcuClaw MCU project.

These commands are registered through
``realtek-app/applications/hmi/west_commands_extention/west-commands.yml``
which is referenced by the manifest at
``realtek-app/applications/hmi/manifest/rtl8773g-zephyr-hmi.yml``.

They are thin wrappers around the stock ``west build`` / ``west flash``
commands that pre-fill the application path and the default board, so the
developer only needs to type::

    west hmi-build
    west hmi-flash

instead of::

    west build -b rtl87x3g_watch/rtl8783gbf \
        realtek-app/applications/hmi/RustMcuClaw/mcu

Any extra arguments passed on the command line are forwarded verbatim to
the underlying ``west`` invocation, e.g.::

    west hmi-build -p always -- -DEXTRA_CONF_FILE=prj_debug.conf
"""

from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

from west.commands import WestCommand


# ---------------------------------------------------------------------------
# Project layout helpers
# ---------------------------------------------------------------------------

# Path of this file:
#   <topdir>/realtek-app/applications/hmi/west_commands_extention/commands.py
#
# We resolve the interesting paths relative to it so the commands keep
# working no matter where the user calls ``west`` from.
_THIS_FILE = Path(__file__).resolve()
_HMI_DIR = _THIS_FILE.parent.parent              # .../applications/hmi
_APP_DIR = _HMI_DIR / "RustMcuClaw" / "mcu"      # the Zephyr application
_DEFAULT_BOARD = "rtl87x3g_watch/rtl8783gbf"
_DEFAULT_BUILD_DIR = _APP_DIR / "build"


def _run_west(cmd_args: list[str]) -> int:
    """Re-invoke ``west`` as a subprocess with the given arguments.

    Using a subprocess (instead of importing west internals) keeps this
    extension forward-compatible with future west releases.  We prefer
    the ``west`` launcher already on PATH so we inherit the user's
    environment exactly; falling back to ``python -m west`` only when
    the launcher cannot be found.
    """
    west_exe = shutil.which("west")
    if west_exe:
        argv = [west_exe, *cmd_args]
    else:
        argv = [sys.executable, "-m", "west", *cmd_args]
    return subprocess.call(argv)


# ---------------------------------------------------------------------------
# west hmi-build
# ---------------------------------------------------------------------------


class HmiBuild(WestCommand):
    def __init__(self) -> None:
        super().__init__(
            "hmi-build",
            "build the HMI RustMcuClaw MCU application",
            "Wrapper around `west build` for "
            "realtek-app/applications/hmi/RustMcuClaw/mcu.",
            accepts_unknown_args=True,
        )

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(
            self.name,
            help=self.help,
            description=self.description,
        )
        parser.add_argument(
            "-b", "--board",
            default=_DEFAULT_BOARD,
            help=f"target board (default: {_DEFAULT_BOARD})",
        )
        parser.add_argument(
            "-d", "--build-dir",
            default=str(_DEFAULT_BUILD_DIR),
            help=f"build directory (default: {_DEFAULT_BUILD_DIR})",
        )
        parser.add_argument(
            "-p", "--pristine",
            choices=("auto", "always", "never"),
            help="pristine build mode forwarded to `west build`",
        )
        parser.add_argument(
            "app_path",
            nargs="?",
            default=str(_APP_DIR),
            help=f"Zephyr application path (default: {_APP_DIR})",
        )
        return parser

    def do_run(self, args, unknown_args):
        if not _APP_DIR.is_dir():
            self.die(f"application directory not found: {_APP_DIR}")

        west_args: list[str] = ["build", "-b", args.board,
                                "-d", args.build_dir]
        if args.pristine:
            west_args += ["-p", args.pristine]
        west_args.append(args.app_path)
        if unknown_args:
            # Anything after `--` (or unrecognised flags) is forwarded to
            # the underlying CMake invocation through west build.
            west_args += list(unknown_args)

        self.inf(f"=> west {' '.join(west_args)}")
        rc = _run_west(west_args)
        if rc != 0:
            self.die(f"west build failed with exit code {rc}")


# ---------------------------------------------------------------------------
# west hmi-flash
# ---------------------------------------------------------------------------


class HmiFlash(WestCommand):
    def __init__(self) -> None:
        super().__init__(
            "hmi-flash",
            "flash the HMI RustMcuClaw MCU application",
            "Wrapper around `west flash` that defaults the build directory "
            "to the one used by `west hmi-build`.",
            accepts_unknown_args=True,
        )

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(
            self.name,
            help=self.help,
            description=self.description,
        )
        parser.add_argument(
            "-d", "--build-dir",
            default=str(_DEFAULT_BUILD_DIR),
            help=f"build directory (default: {_DEFAULT_BUILD_DIR})",
        )
        return parser

    def do_run(self, args, unknown_args):
        if not Path(args.build_dir).is_dir():
            self.die(
                f"build directory not found: {args.build_dir} "
                "(run `west hmi-build` first)"
            )
        west_args = ["flash", "-d", args.build_dir]
        if unknown_args:
            west_args += list(unknown_args)
        self.inf(f"=> west {' '.join(west_args)}")
        rc = _run_west(west_args)
        if rc != 0:
            self.die(f"west flash failed with exit code {rc}")


# ---------------------------------------------------------------------------
# west hmi-clean
# ---------------------------------------------------------------------------


class HmiClean(WestCommand):
    def __init__(self) -> None:
        super().__init__(
            "hmi-clean",
            "remove the HMI build directory",
            "Delete the build directory used by `west hmi-build`.",
        )

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(
            self.name,
            help=self.help,
            description=self.description,
        )
        parser.add_argument(
            "-d", "--build-dir",
            default=str(_DEFAULT_BUILD_DIR),
            help=f"build directory (default: {_DEFAULT_BUILD_DIR})",
        )
        return parser

    def do_run(self, args, unknown_args):
        build_dir = Path(args.build_dir)
        if build_dir.is_dir():
            self.inf(f"removing {build_dir}")
            shutil.rmtree(build_dir)
        else:
            self.inf(f"nothing to do: {build_dir} does not exist")
