#!/usr/bin/env python3
"""
SDK Generation Script
Based on release_package_config defined in SDK_COMMON.py / SDK_HMI.py.

This script reads the src_files add_list / remove_list from the release config and
produces a trimmed SDK package by copying files from a local SDK workspace — without
requiring any compilation.

Usage:
    python generate_sdk.py
        --workspace <workspace_root>        # directory that contains the sdk/ subfolder
        --output    <output_dir>            # destination SDK package directory
        [--config   <path_to_SDK_XXX.py>]  # release config file (default: SDK_COMMON.py)
        [--package  <package_key>]          # config key to process  (default: sdk_package)
        [--patch    <patch.json>]           # optional JSON patch (see below)
        [--dry-run]                         # preview without writing any files
        [--quiet]                           # suppress per-file output

Patch file format (JSON):
    {
        "_comment": "optional description",

        "replace_add": {
            "sdk/board/evb/hmi": [
                ["sdk/board/evb/hmi_app/chargecase", "board/evb/hmi_app/chargecase"],
                ["sdk/board/evb/hmi_app/dashboard",  "board/evb/hmi_app/dashboard"]
            ]
        },

        "extra_add": [
            ["sdk/bin/rtl87x3ep/hal_utils.lib", "bin/rtl87x3ep/hal_utils.lib"],
            ["sdk/bin/rtl87x3ep/default_bin",   "bin/rtl87x3ep/default_bin"],
            ["sdk/src/sample/gui",               "src/sample/gui"]
        ],

        "extra_remove": [
            "src/sample/gui/keil_sim",
            "src/sample/gui/.git",
            ["src/sample/gui", ".*\\\\.o$"],
            ["src/sample/gui", "Objects$"]
        ]
    }

    Keys:
      replace_add  — substitute a named add_list entry with one or more replacements.
                     Key is the original source path string from the config.
      extra_add    — additional add_list entries processed after the main config blocks.
      extra_remove — additional remove_list entries processed after extra_add.

    add_list entry formats:
      "sdk/path/dir"                           → copy dir, strip repo prefix for dst
      ["sdk/path/src", "dst/path"]             → explicit src → dst
      ["sdk/path/src", "regex", "dst/path"]    → copy only regex-matching files

    remove_list entry formats:
      "output/path/to/delete"                  → remove that path from output
      ["output/search_dir", "regex"]           → remove every matching entry in dir

Examples:
    # Basic — use default SDK_COMMON.py config
    python generate_sdk.py \\
        --workspace D:/repos/rtl87x3ep \\
        --output    D:/out/sdk_pkg

    # HMI SDK with customisation patch
    python generate_sdk.py \\
        --workspace D:/Project/HoneyHmi/honeycomb \\
        --output    D:/Project/HoneyHmi/sdk_local \\
        --config    honeycomb/script/build/release_config/rtl87x3ep/SDK_HMI.py \\
        --package   sdk_package \\
        --patch     sdk_hmi_patch.json

    # Dry-run preview (no files written)
    python generate_sdk.py \\
        --workspace D:/Project/HoneyHmi/honeycomb \\
        --output    D:/Project/HoneyHmi/sdk_pk \\
        --config    honeycomb/script/build/release_config/rtl87x3ep/SDK_HMI.py \\
        --patch     sdk_hmi_patch.json \\
        --dry-run
"""

import os
import re
import sys
import shutil
import argparse
import importlib.util
from pathlib import Path


# ---------------------------------------------------------------------------
# Config loading
# ---------------------------------------------------------------------------

def load_release_config(config_path: str) -> dict:
    """Exec the config .py file and return its release_package_config dict."""
    spec = importlib.util.spec_from_file_location("_sdk_cfg", config_path)
    mod = importlib.util.module_from_spec(spec)
    # Suppress the os.environ side-effect – harmless but we note it here
    spec.loader.exec_module(mod)
    return mod.release_package_config


# ---------------------------------------------------------------------------
# Path helpers
# ---------------------------------------------------------------------------

RENAME_SUFFIX = "(rename)"


def _norm(p: str) -> str:
    """Normalise to OS separator."""
    return p.replace("/", os.sep).replace("\\", os.sep)


def src_abs(workspace: str, path_in_config: str) -> str:
    """
    Convert a config source path to an absolute filesystem path.
    Config paths are relative to workspace, e.g. "sdk/src/ble".
    """
    return os.path.join(workspace, _norm(path_in_config))


def dst_abs(output: str, path_in_config: str) -> tuple[str, bool]:
    """
    Convert a config destination path to an absolute filesystem path.
    Returns (abs_path, is_rename).

    The special suffix '(rename)' means the file should be placed with
    the bare filename instead of its source name, e.g.
        ("sdk/Kconfig.release", "Kconfig(rename)") -> output/Kconfig
    """
    renamed = path_in_config.endswith(RENAME_SUFFIX)
    clean = path_in_config.replace(RENAME_SUFFIX, "").rstrip("/\\")
    return os.path.join(output, _norm(clean)), renamed


def strip_repo_prefix(path: str, repo: str) -> str:
    """
    Strip a leading "<repo>/" prefix from path so it becomes output-relative.
    e.g. "sdk/src/ble" -> "src/ble"  when repo == "sdk"
    """
    prefix = repo + "/"
    if path.startswith(prefix):
        return path[len(prefix):]
    return path


# ---------------------------------------------------------------------------
# File operations
# ---------------------------------------------------------------------------

def _copy_file(src: str, dst: str, dry: bool, verbose: bool):
    if not os.path.isfile(src):
        print(f"  [SKIP] Not a file: {src}")
        return
    if verbose:
        print(f"  [FILE] {src}\n      -> {dst}")
    if not dry:
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(src, dst)


def _copy_dir(src: str, dst: str, dry: bool, verbose: bool):
    if not os.path.isdir(src):
        print(f"  [SKIP] Not a dir: {src}")
        return
    if verbose:
        print(f"  [DIR ] {src}\n      -> {dst}")
    if not dry:
        if os.path.exists(dst):
            shutil.rmtree(dst)
        shutil.copytree(src, dst)


def copy_item(src: str, dst: str, dry: bool, verbose: bool):
    """Copy a file or directory from src to dst."""
    if not os.path.exists(src):
        print(f"  [SKIP] Source not found: {src}")
        return
    if os.path.isfile(src):
        _copy_file(src, dst, dry, verbose)
    else:
        _copy_dir(src, dst, dry, verbose)


def copy_regex_match(src_dir: str, pattern: str, dst_dir: str, dry: bool, verbose: bool):
    """
    Walk src_dir, copy every entry whose relative path matches *pattern* to dst_dir.
    Pattern is matched against the full relative path (forward-slash form).
    """
    if not os.path.exists(src_dir):
        print(f"  [SKIP] Regex src not found: {src_dir}")
        return
    rx = re.compile(pattern)
    for root, dirs, files in os.walk(src_dir):
        for name in files + dirs:
            full = os.path.join(root, name)
            rel = os.path.relpath(full, src_dir).replace("\\", "/")
            if rx.search(rel):
                dst = os.path.join(dst_dir, _norm(rel))
                copy_item(full, dst, dry, verbose)


def remove_item(path: str, dry: bool, verbose: bool, _from_regex: bool = False):
    if not os.path.exists(path):
        # In dry-run mode always announce the intended removal even if the
        # output tree hasn't been created yet.
        if dry and verbose and not _from_regex:
            print(f"  [DEL ?   ] {path}  (output not yet created)")
        return
    if verbose:
        kind = "DIR " if os.path.isdir(path) else "FILE"
        print(f"  [DEL {kind}] {path}")
    if not dry:
        if os.path.isfile(path):
            os.remove(path)
        elif os.path.isdir(path):
            shutil.rmtree(path)


def remove_regex_match(base_dir: str, pattern: str, dry: bool, verbose: bool):
    """
    Walk base_dir (already absolute), remove every entry whose name or
    relative path matches *pattern*.
    Walk bottom-up so directories are deleted after their contents.
    """
    if not os.path.exists(base_dir):
        if dry and verbose:
            print(f"  [DEL rx? ] {base_dir}  pattern='{pattern}'  (output not yet created)")
        return
    rx = re.compile(pattern)
    # collect then sort by depth (deepest first) to avoid double-removal errors
    to_remove = []
    for root, dirs, files in os.walk(base_dir, topdown=True):
        for name in files + dirs:
            full = os.path.join(root, name)
            rel = os.path.relpath(full, base_dir).replace("\\", "/")
            # match against both the bare name and the relative path
            if rx.search(name) or rx.search(rel):
                to_remove.append(full)
        # Prune dirs we will already delete (avoid redundant walk)
        dirs[:] = [d for d in dirs
                   if not rx.search(d)
                   and not rx.search(os.path.relpath(os.path.join(root, d), base_dir).replace("\\", "/"))]

    # Sort deepest-first so parent removal doesn't fail
    to_remove.sort(key=lambda p: p.count(os.sep), reverse=True)
    for p in to_remove:
        remove_item(p, dry, verbose)


# ---------------------------------------------------------------------------
# Core processing
# ---------------------------------------------------------------------------

def process_add_list(add_list: list, workspace: str, output: str,
                     repo: str, dry: bool, verbose: bool):
    """
    Process an add_list according to three possible entry formats:

    1. String  "sdk/path/to/dir"
       src = workspace/sdk/path/to/dir
       dst = output/path/to/dir   (repo prefix stripped)

    2. 2-tuple ("sdk/path/src", "path/dst")
       src = workspace/sdk/path/src
       dst = output/path/dst
       (if dst ends with '(rename)' the file is placed as output/<bare_name>)

    3. 3-tuple ("sdk/path/src", "regex_pattern", "path/dst")
       Copy files matching regex inside src_dir to dst_dir
    """
    for entry in add_list:
        if isinstance(entry, str):
            src = src_abs(workspace, entry)
            dst = os.path.join(output, _norm(strip_repo_prefix(entry, repo)))
            copy_item(src, dst, dry, verbose)

        elif isinstance(entry, (list, tuple)):
            if len(entry) == 2:
                src_rel, dst_rel = entry
                src = src_abs(workspace, src_rel)
                dst, _ = dst_abs(output, dst_rel)
                copy_item(src, dst, dry, verbose)

            elif len(entry) == 3:
                src_rel, pattern, dst_rel = entry
                src = src_abs(workspace, src_rel)
                dst, _ = dst_abs(output, dst_rel)
                copy_regex_match(src, pattern, dst, dry, verbose)

            else:
                print(f"  [WARN] Unhandled add_list entry (len={len(entry)}): {entry}")
        else:
            print(f"  [WARN] Unknown add_list entry type: {entry!r}")


def process_remove_list(remove_list: list, output: str, dry: bool, verbose: bool):
    """
    Process a remove_list.  Two formats:

    1. String  "path/to/remove"
       Remove output/path/to/remove

    2. 2-tuple ("search_dir", "regex_pattern")
       Remove every file/dir inside output/search_dir whose path matches pattern
    """
    for entry in remove_list:
        if isinstance(entry, str):
            path = os.path.join(output, _norm(entry))
            remove_item(path, dry, verbose)

        elif isinstance(entry, (list, tuple)) and len(entry) == 2:
            search_dir, pattern = entry
            base = os.path.join(output, _norm(search_dir))
            remove_regex_match(base, pattern, dry, verbose)

        else:
            print(f"  [WARN] Unknown remove_list entry: {entry!r}")


def load_patches(patch_file: str) -> dict:
    """
    Load a JSON patch file.  Format:
    {
        "extra_add": [
            ["sdk/board/evb/hmi_app/chargecase", "board/evb/hmi_app/chargecase"],
            ["sdk/board/evb/hmi_app/dashboard",  "board/evb/hmi_app/dashboard"]
        ],
        "extra_remove": [
            "board/evb/hmi",
            ["board/evb/hmi_app", ".*\\.gitignore"]
        ],
        "replace_add": {
            "sdk/board/evb/hmi": ["sdk/board/evb/hmi_app/chargecase", "board/evb/hmi_app/chargecase"]
        }
    }

    Keys:
      extra_add    – list of add_list entries appended after main add processing
      extra_remove – list of remove_list entries appended after main remove processing
      replace_add  – dict mapping an existing src path to a replacement add_list entry
                     (removes the original entry, inserts the new one)
    """
    import json
    with open(patch_file, encoding="utf-8") as f:
        return json.load(f)


def apply_replace_add(add_list: list, replace_map: dict) -> list:
    """Replace specific add_list entries based on replace_add patch map."""
    result = []
    for entry in add_list:
        src_key = None
        if isinstance(entry, str):
            src_key = entry
        elif isinstance(entry, (list, tuple)) and len(entry) >= 2:
            src_key = entry[0]

        if src_key and src_key in replace_map:
            replacement = replace_map[src_key]
            print(f"  [PATCH] Replacing '{src_key}' -> {replacement}")
            if isinstance(replacement[0], (list, tuple)):
                result.extend(replacement)
            else:
                result.append(replacement)
        else:
            result.append(entry)
    return result


def generate_package(config: dict, package_key: str,
                     workspace: str, output: str,
                     dry: bool, verbose: bool,
                     patches=None):
    """
    Main entry: generate the SDK package for *package_key* into *output*.
    Only processes src_files entries where copy_from refers to a real repo
    (i.e. not another generated package).  copy_from values like "sdk_package"
    or "qc_package" are skipped — those require a prior packaging step.

    patches: optional dict loaded from a JSON patch file (see load_patches).
    """
    pkg = config.get(package_key)
    if pkg is None:
        print(f"[ERROR] Package key '{package_key}' not found in config.")
        print(f"        Available keys: {list(config.keys())}")
        sys.exit(1)

    src_files = pkg.get("src_files", [])
    if not src_files:
        print(f"[WARN] No src_files found for package '{package_key}'.")
        return

    replace_map = (patches or {}).get("replace_add", {})

    for idx, block in enumerate(src_files):
        copy_from = block.get("copy_from", "sdk")
        add_list   = block.get("add_list",    [])
        remove_list = block.get("remove_list", [])

        # Only handle blocks that copy from a real directory on disk.
        # Blocks with copy_from = "sdk_package" / "qc_package" etc. reference
        # a previously generated package — skip those.
        real_repos = {"sdk", "hal", "sys-patch", "keil_proj"}
        if copy_from not in real_repos:
            print(f"\n[BLOCK {idx}] copy_from='{copy_from}' is a derived package — skipped.")
            continue

        # Apply replace_add patches
        if replace_map:
            add_list = apply_replace_add(add_list, replace_map)

        print(f"\n{'='*70}")
        print(f"[BLOCK {idx}] copy_from='{copy_from}'  ({len(add_list)} add, {len(remove_list)} remove)")
        print(f"{'='*70}")

        print(f"\n--- ADD LIST ---")
        process_add_list(add_list, workspace, output, copy_from, dry, verbose)

        print(f"\n--- REMOVE LIST ---")
        process_remove_list(remove_list, output, dry, verbose)

    # Apply extra_add / extra_remove patches
    if patches:
        extra_add = patches.get("extra_add", [])
        extra_remove = patches.get("extra_remove", [])
        if extra_add or extra_remove:
            print(f"\n{'='*70}")
            print(f"[PATCH] Applying extra add/remove from patch file")
            print(f"{'='*70}")
            if extra_add:
                print(f"\n--- PATCH ADD LIST ---")
                process_add_list(extra_add, workspace, output, "sdk", dry, verbose)
            if extra_remove:
                print(f"\n--- PATCH REMOVE LIST ---")
                process_remove_list(extra_remove, output, dry, verbose)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def parse_args():
    script_dir = Path(__file__).resolve().parent
    repo_root = script_dir.parent.parent  # HoneyComb root (script lives at script/hmi_script/)

    default_workspace = str(repo_root)
    default_output = str(repo_root / "build" / "sdk_package")
    default_config = str(
        repo_root / "script" / "build" /
        "release_config" / "rtl87x3ep" / "SDK_COMMON.py"
    )

    p = argparse.ArgumentParser(
        description="Generate a trimmed SDK package from a local workspace "
                    "using release_package_config from SDK_COMMON.py / SDK_HMI.py.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    p.add_argument(
        "--workspace", default=default_workspace,
        help=f"Path to the workspace root that contains the 'sdk/' subfolder. Default: {default_workspace}",
    )
    p.add_argument(
        "--output", default=default_output,
        help=f"Path where the generated SDK package will be written. Default: {default_output}",
    )
    p.add_argument(
        "--config", default=default_config,
        help=f"Path to the release config .py file. Default: {default_config}",
    )
    p.add_argument(
        "--package", default="sdk_package",
        help="Key inside release_package_config to process. Default: sdk_package",
    )
    p.add_argument(
        "--dry-run", action="store_true",
        help="Preview operations without actually copying or deleting files.",
    )
    p.add_argument(
        "--patch", default=None, metavar="PATCH_JSON",
        help="Optional JSON patch file for extra add/remove steps or add-entry replacements.",
    )
    p.add_argument(
        "--quiet", action="store_true",
        help="Suppress per-file output (still prints block headers and warnings).",
    )
    return p.parse_args()


def main():
    args = parse_args()

    workspace = os.path.abspath(args.workspace)
    output    = os.path.abspath(args.output)
    config_path = os.path.abspath(args.config)
    dry  = args.dry_run
    verbose = not args.quiet

    # Validate
    if not os.path.isdir(workspace):
        print(f"[ERROR] Workspace not found: {workspace}")
        sys.exit(1)
    if not os.path.isfile(config_path):
        print(f"[ERROR] Config file not found: {config_path}")
        sys.exit(1)
    sdk_dir = os.path.join(workspace, "sdk")
    if not os.path.isdir(sdk_dir):
        print(f"[WARN] Expected 'sdk/' subfolder not found at: {sdk_dir}")
        print(f"       Proceeding anyway — some copy operations may be skipped.")

    # Load optional patch file
    patches = None
    if args.patch:
        patch_path = os.path.abspath(args.patch)
        if not os.path.isfile(patch_path):
            print(f"[ERROR] Patch file not found: {patch_path}")
            sys.exit(1)
        patches = load_patches(patch_path)
        print(f"Patch     : {patch_path}")

    print(f"Workspace : {workspace}")
    print(f"Output    : {output}")
    print(f"Config    : {config_path}")
    print(f"Package   : {args.package}")
    print(f"Dry run   : {dry}")
    print()

    if dry:
        print("*** DRY RUN — no files will be modified ***\n")

    # Load config
    try:
        config = load_release_config(config_path)
    except Exception as e:
        print(f"[ERROR] Failed to load config: {e}")
        sys.exit(1)

    if not dry:
        os.makedirs(output, exist_ok=True)

    generate_package(config, args.package, workspace, output, dry, verbose, patches)

    print(f"\n{'='*70}")
    if dry:
        print("Dry run complete.  Re-run without --dry-run to apply changes.")
    else:
        print(f"Done.  SDK package written to: {output}")


if __name__ == "__main__":
    main()
