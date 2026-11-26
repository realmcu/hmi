#!/usr/bin/env python3
# Copyright (c) 2026, Realtek Semiconductor Corporation
# SPDX-License-Identifier: Apache-2.0
"""eBadge 打包工具。

三个子命令：

  check   把 tools/pack/layout.json 和工程里各处硬编码的地址/大小逐一比对，
          找出还没同步的地方。改 flash 规划后先跑这个。

  app     把 app 镜像和 app_romfs.bin 合成一个 bin：按固定地址放置 romfs、
          重定位 romfs 里的绝对指针、更新镜像头 payload_len、重新签名、
          可选加 MP 头。产物可直接烧到 app 地址，也可作为 OTA 的单个镜像。

  fdb     用固件同一份 FlashDB 源码在 PC 上生成预制资源的 FDB 镜像
          （KVDB + BF 两个分区，含目录项），烧到 kvdb 起始地址即可。

为什么 romfs 要重定位：mkromfs 生成的目录项里 name/data 都是**绝对 flash
地址**；romfs 换一个落点，这些指针就全错了，而 honeygui 的 romfs 后端是
直接解引用、不支持基址偏移（gui_vfs_mount_romfs 的 base_offset 参数没有被
使用）。所以这里按 delta 重写所有可达目录项。
"""

import argparse
import glob
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

IS_WIN = sys.platform == "win32"

HERE = os.path.dirname(os.path.abspath(__file__))
APP_ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
REALTEK_APP = os.path.abspath(os.path.join(APP_ROOT, "..", ".."))
GADGETS = os.path.join(REALTEK_APP, "tools", "Gadgets")
FLASHDB = os.path.join(APP_ROOT, "component", "FlashDB")

HDR_SIZE = 0x400          # image header (T_IMG_HEADER_FORMAT)
MP_HDR_SIZE = 0x200       # MP header
HDRSIZE = 0x400


# --------------------------------------------------------------------------
# small helpers
# --------------------------------------------------------------------------

def die(msg, rc=1):
    sys.stderr.write("pack: error: %s\n" % msg)
    sys.exit(rc)


def warn(msg):
    sys.stderr.write("pack: warning: %s\n" % msg)


def info(msg):
    sys.stderr.write("pack: %s\n" % msg)


def u32(b, off):
    return struct.unpack_from("<I", b, off)[0]


def hexs(v):
    return "0x%08X" % v


def load_layout(path=None):
    path = path or os.path.join(HERE, "layout.json")
    with open(path, "r", encoding="utf-8") as f:
        raw = json.load(f)

    def conv(v):
        if isinstance(v, dict):
            return {k: conv(x) for k, x in v.items()}
        if isinstance(v, list):
            return [conv(x) for x in v]
        if isinstance(v, str) and v.startswith("0x"):
            return int(v, 16)
        return v

    return conv(raw), path


def run(cmd, **kw):
    shown = " ".join(cmd) if isinstance(cmd, list) else cmd
    if kw.pop("verbose", True):
        info("$ " + shown)
    return subprocess.run(cmd, **kw)


def gadget(name):
    """Gadgets 目录同时提供 ELF 与 .exe；Windows 上必须选 .exe。"""
    base = os.path.join(GADGETS, name)
    if IS_WIN and os.path.exists(base + ".exe"):
        return base + ".exe"
    return base


def host_cc():
    """host 端 C 编译器（生成 FDB 镜像用）。"""
    if os.environ.get("CC"):
        return os.environ["CC"]
    if IS_WIN:
        for cand in ("gcc", "clang", "cc"):
            found = shutil.which(cand)
            if found:
                return found
        die("Windows 上生成 FDB 镜像需要 host C 编译器。\n"
            "     请安装 MinGW-w64（gcc）并加入 PATH，或设置 CC=<gcc.exe 路径>。")
    return "cc"


# --------------------------------------------------------------------------
# romfs: parse + relocate
# --------------------------------------------------------------------------

ROMFS_DIRENT = struct.Struct("<IIII")   # type, name*, data*, size
ROMFS_DIRENT_DIR = 1


def _walk_dirents(buf, off, old_base, seen, out):
    """收集所有可达目录项的 (off, name_ptr, data_ptr, size)。递归目录。"""
    if off in seen:
        return
    seen.add(off)
    if off + ROMFS_DIRENT.size > len(buf):
        die("romfs 目录项越界 @0x%X" % off)
    typ, name_p, data_p, size = ROMFS_DIRENT.unpack_from(buf, off)
    if typ not in (0, ROMFS_DIRENT_DIR):
        return
    out.append((off, name_p, data_p, size, typ))
    if typ == ROMFS_DIRENT_DIR:
        # For an empty directory (size==0) the data pointer points at the "start" of the child array, which
        # is often exactly the end of the file — e.g. the `user` directory comes last. There are no children to
        # visit then, so an out-of-range pointer is not an error and it is simply relocated too. Check size before bounds.
        if size == 0:
            return
        child_off = data_p - old_base
        if child_off < 0 or child_off + ROMFS_DIRENT.size > len(buf):
            die("romfs 子目录指针越界 @0x%X (dir off 0x%X)" % (data_p, off))
        for i in range(size):
            _walk_dirents(buf, child_off + i * ROMFS_DIRENT.size,
                          old_base, seen, out)


def romfs_infer_base(buf):
    """从根目录项反推原基址：根目录 0，name 指向的字符串紧随其后。"""
    if len(buf) < ROMFS_DIRENT.size:
        die("romfs 文件太短")
    typ, name_p, data_p, size = ROMFS_DIRENT.unpack_from(buf, 0)
    if typ != ROMFS_DIRENT_DIR:
        die("romfs 根不是目录 (type=%d)" % typ)
    # mkromfs' fixed layout: root dirent 16B, name right after it, children 4B later
    base = name_p - ROMFS_DIRENT.size
    if data_p - base != ROMFS_DIRENT.size + 4:
        die("romfs 根布局不是预期形式（name=0x%X data=0x%X），"
            "请用 --romfs-old-base 显式指定原始基址" % (name_p, data_p))
    return base


def romfs_relocate(buf, new_base, old_base=None):
    if old_base is None:
        old_base = romfs_infer_base(buf)
    delta = new_base - old_base
    entries = []
    _walk_dirents(buf, 0, old_base, set(), entries)

    for off, name_p, data_p, size, typ in entries:
        struct.pack_into("<I", buf, off + 4, name_p + delta)
        struct.pack_into("<I", buf, off + 8, data_p + delta)

    # post-relocation self-check: every pointer must fall inside this romfs
    lo, hi = new_base, new_base + len(buf)
    for off, name_p, data_p, size, typ in entries:
        n = name_p + delta
        d = data_p + delta
        if not (lo <= n < hi):
            die("重定位后 name 指针越界 @%s" % hexs(n))
        if not (lo <= d <= hi):
            die("重定位后 data 指针越界 @%s" % hexs(d))
    return old_base, new_base, len(entries)


def romfs_list(buf):
    """列出目录项（调试/校验用）。"""
    old_base = romfs_infer_base(buf)
    entries = []
    _walk_dirents(buf, 0, old_base, set(), entries)
    rows = []
    for off, name_p, data_p, size, typ in entries:
        no = name_p - old_base
        end = buf.index(b"\0", no) if b"\0" in buf[no:] else len(buf)
        name = buf[no:end].decode("utf-8", "replace")
        rows.append(("dir " if typ == ROMFS_DIRENT_DIR else "file", name, size,
                     data_p - old_base))
    return old_base, rows


# --------------------------------------------------------------------------
# image header
# --------------------------------------------------------------------------

IMG_CTRL_OFF = 196        # offsetof(T_IMG_HEADER_FORMAT, ctrl_header)


def read_img_header(buf):
    if buf[:6] != b"IMGHDR":
        die("输入不是带镜像头的 bin（magic 不是 IMGHDR）——"
            "请传 bin/<bank>/app_<bank>.bin，而不是 zephyr.bin 的裸 payload")
    ctrl_flag, ic_type, sec_ver, img_id, hdr_len, payload_len = \
        struct.unpack_from("<IBBHII", buf, IMG_CTRL_OFF)
    return dict(ctrl_flag=ctrl_flag, ic_type=ic_type, sec_ver=sec_ver,
                img_id=img_id, hdr_len=hdr_len, payload_len=payload_len)


# --------------------------------------------------------------------------
# check
# --------------------------------------------------------------------------

def _eval_c_int(expr):
    """求值 C 里的简单整数表达式：(256UL * 1024)、0x1000、4096u 等。

    只接受数字、括号和 + - * / 和位移；出现任何别的东西就返回 None，
    宁可报"缺失"也不猜。
    """
    e = re.sub(r"\b(0[xX][0-9A-Fa-f]+|\d+)[uUlL]+\b", r"\1", expr)
    if not re.fullmatch(r"[0-9A-Fa-fxX()+\-*/\s<>|&]+", e):
        return None
    try:
        return int(eval(e, {"__builtins__": {}}, {}))  # noqa: S307 - charset already restricted
    except Exception:
        return None


def _parse_defines(text, prefix_re):
    out = {}
    for m in re.finditer(r"#define\s+(\w+)\s+(.+)", text):
        name, raw = m.group(1), m.group(2).split("//")[0].split("/*")[0].strip()
        if not prefix_re.search(name):
            continue
        v = _eval_c_int(raw)
        if v is not None:
            out[name] = v
    return out


def cmd_check(args):
    lay, lay_path = load_layout(args.layout)
    problems = []

    stale = []          # differences of flash variants the project no longer uses (16M/8M/4M)

    def cmp(name, got, want, where, target=None):
        bucket = problems if target is None else target
        if got is None:
            bucket.append("%-26s 缺失            (%s)" % (name, where))
        elif got != want:
            bucket.append("%-26s 工程=%s  layout=%s   (%s)"
                          % (name, hexs(got), hexs(want), where))

    # --- flash_map.h ---
    fm_path = os.path.join(APP_ROOT, "flash_map.h")
    if os.path.exists(fm_path):
        with open(fm_path, encoding="utf-8", errors="replace") as f:
            fm = _parse_defines(f.read(), re.compile(r"^(BANK[01]_|USER_DATA)"))
        for bank, b in lay["banks"].items():
            up = bank.upper()
            cmp("%s_APP_ADDR" % up, fm.get("%s_APP_ADDR" % up),
                b["app_addr"], "flash_map.h")
            cmp("%s_APP_SIZE" % up, fm.get("%s_APP_SIZE" % up),
                b["app_size"], "flash_map.h")
        # USER_DATA1 is zeroed out and its region given to FlashDB: its start must match the FDB start,
        # and its size must be 0 (otherwise OTA still writes resource images into the FDB region).
        cmp("USER_DATA1_ADDR", fm.get("USER_DATA1_ADDR"),
            lay["fdb"]["kvdb_addr"], "flash_map.h")
        cmp("USER_DATA1_SIZE", fm.get("USER_DATA1_SIZE"), 0, "flash_map.h")
    else:
        problems.append("flash_map.h 不存在")

    # --- snippets overlay (partition table) ---
    # Both SNIPPET_ROOTs must be scanned: under realtek-app are the variants shared by all apps (16M/8M/4M),
    # and under this project's own snippets/ is the 32M version currently in use. That way no copy is missed when the layout changes.
    # Same names prefer this project — matching Zephyr's behavior: a name present in both places appends the overlay twice,
    # and this project's comes later, so it is the one that ends up in effect.
    snip_roots = [os.path.join(REALTEK_APP, "snippets"),
                  os.path.join(APP_ROOT, "snippets")]
    snip_paths = {}
    for root in snip_roots:
        if not os.path.isdir(root):
            continue
        for d in os.listdir(root):
            if re.fullmatch(r"flash_\d+M_bank[01]", d):
                snip_paths[d] = os.path.join(root, d)
    snip_variants = sorted(snip_paths)
    # layout.json's flash.size decides which variant is "currently in use":
    #   flash 32M -> flash_32M_* is the live configuration, the rest are optional spare variants
    active_mb = lay["flash"]["size"] // (1024 * 1024)
    for snip in snip_variants:
        is_active = snip.startswith("flash_%dM_" % active_mb)
        tgt = None if is_active else stale
        bank = "bank1" if snip.endswith("bank1") else "bank0"
        b = lay["banks"][bank]
        ov = os.path.join(snip_paths[snip], snip + ".overlay")
        with open(ov, encoding="utf-8", errors="replace") as f:
            text = f.read()
        m = re.search(r'flash:\s*flash@70000000\s*\{(.*?)\n\t\};', text, re.S)
        if m:
            regions = re.findall(r"reg\s*=\s*<(0x[0-9A-Fa-f]+)\s+DT_SIZE_K\((\d+)\)>",
                                 m.group(1))
            total = [(int(a, 0), int(k, 0) * 1024) for a, k in regions]
            if total:
                last_a, last_s = total[-1]
                end = last_a + last_s
                if end > lay["flash"]["size"]:
                    tgt.append("%-26s overlay 越界: 末尾 %s > flash %s"
                               % (snip, hexs(end), hexs(lay["flash"]["size"])))
        # app partition
        m = re.search(r"bank_app_partition:\s*partition@([0-9A-Fa-f]+)\s*\{"
                      r".*?reg\s*=\s*<0x[0-9A-Fa-f]+\s+DT_SIZE_K\((\d+)\)>",
                      text, re.S)
        if m:
            cmp("%s: app 分区" % snip,
                int(m.group(1), 16) + lay["flash"]["base"],
                b["app_addr"], "overlay", tgt)
            cmp("%s: app 大小" % snip,
                int(m.group(2)) * 1024, b["app_size"], "overlay", tgt)
        # flash capacity declared to DT: the 32M version must be able to hold the end of the layout
        msize = re.search(r"flash:\s*flash@70000000\s*\{[^}]*?"
                          r"reg\s*=\s*<0x[0-9A-Fa-f]+\s+DT_SIZE_K\((\d+)\)>",
                          text, re.S)
        if msize:
            dtfl = int(msize.group(1)) * 1024
            tail = max((r["addr"] + r["size"] for r in lay["reserved_regions"]),
                       default=0) - lay["flash"]["base"]
            if tail > dtfl:
                tgt.append("%-26s DT flash=%s 装不下布局末尾 %s（相对）"
                           % (snip, hexs(dtfl), hexs(tail)))

    # --- fal_cfg.h ---
    fal = os.path.join(APP_ROOT, "port", "flashdb", "fal_cfg.h")
    if os.path.exists(fal):
        with open(fal, encoding="utf-8", errors="replace") as f:
            ft = f.read()
        d = _parse_defines(ft, re.compile(r"^(RTK_|FDB_)"))
        fdb = lay["fdb"]
        proj_kvdb = (d.get("RTK_NOR_FLASH_XIP_BASE", 0) +
                     d.get("FDB_REGION_OFFSET", 0))
        proj_bf = proj_kvdb + d.get("FDB_KVDB_SIZE", 0)
        cmp("FDB kvdb 绝对地址", proj_kvdb, fdb["kvdb_addr"], "fal_cfg.h")
        cmp("FDB bf 绝对地址", proj_bf, fdb["bf_data_addr"], "fal_cfg.h")
        cmp("FDB kvdb 大小", d.get("FDB_KVDB_SIZE"), fdb["kvdb_size"], "fal_cfg.h")
        cmp("FDB bf 大小", d.get("FDB_BF_DATA_SIZE"), fdb["bf_data_size"],
            "fal_cfg.h")
        # overflow is checked at both ends: if any part of the BF region falls outside flash, it does not "exist"
        flash_lo = lay["flash"]["base"]
        flash_hi = flash_lo + lay["flash"]["size"]
        for tag, addr, size in (("kvdb", fdb["kvdb_addr"], fdb["kvdb_size"]),
                                ("bf", fdb["bf_data_addr"], fdb["bf_data_size"])):
            if addr < flash_lo or addr + size > flash_hi:
                problems.append("layout 里 FDB %s 区 [%s,%s) 超出 flash [%s,%s)"
                                % (tag, hexs(addr), hexs(addr + size),
                                   hexs(flash_lo), hexs(flash_hi)))
        if proj_bf + d.get("FDB_BF_DATA_SIZE", 0) > flash_hi:
            problems.append("fal_cfg.h 算出的 BF 区末尾 %s 超出 flash 末尾 %s"
                            % (hexs(proj_bf + d.get("FDB_BF_DATA_SIZE", 0)),
                               hexs(flash_hi)))

    # --- romfs mount point ---
    #
    # Two legal forms:
    #   a) absolute constant: gui_vfs_mount_romfs("/", (void *)0x70536400, 0)
    #      -> must equal romfs.addr in the layout
    #   b) run-time offset: ...(__rom_region_start + ROMFS_OFFSET)
    #      -> tied to the packing method and no longer relying on the layout's absolute address; it only checks that ROMFS_OFFSET
    #         is provided by romfs_layout.h and is non-zero
    entry = os.path.join(APP_ROOT, "app", "designer", "src", "Mi_eBadgeEntry.c")
    if os.path.exists(entry):
        with open(entry, encoding="utf-8", errors="replace") as f:
            t = f.read()
        if "__rom_region_start" in t:
            lay_h = None
            for cand in romfs_layout_paths():
                if os.path.exists(cand):
                    lay_h = cand
                    break
            off = None
            if lay_h:
                with open(lay_h, encoding="utf-8") as f:
                    m = re.search(r"#define\s+ROMFS_OFFSET\s+(0x[0-9A-Fa-f]+|\d+)",
                                  f.read())
                off = int(m.group(1), 0) if m else None
            if off is None:
                problems.append("%-26s ROMFS_OFFSET 未定义         (romfs_layout.h)"
                                % "romfs 挂载（运行时偏移）")
            elif off == 0:
                problems.append("%-26s ROMFS_OFFSET=0（尚未打包）  (romfs_layout.h)"
                                % "romfs 挂载（运行时偏移）")
            else:
                info("romfs 挂载用运行时偏移 ROMFS_OFFSET=%s"
                     "（不依赖 layout 的绝对地址）" % hexs(off))
        else:
            m = re.search(r"gui_vfs_mount_romfs\([^,]+,\s*\(void\s*\*\)\s*"
                          r"(0x[0-9A-Fa-f]+)", t)
            cmp("romfs 挂载地址", int(m.group(1), 16) if m else None,
                lay.get("romfs", {}).get("addr"), "Mi_eBadgeEntry.c")
    # project.json's romfsBaseAddr only matters for the "absolute constant" form;
    # with a run-time offset it is only a designer simulation hint, not part of the firmware, so it is not compared.
    uses_runtime = "__rom_region_start" in t
    pj = os.path.join(APP_ROOT, "app", "designer", "project.json")
    if os.path.exists(pj) and not uses_runtime and lay.get("romfs", {}).get("addr"):
        with open(pj, encoding="utf-8") as f:
            pjd = json.load(f)
        cmp("romfsBaseAddr", int(pjd.get("romfsBaseAddr", "0x0"), 16),
            lay["romfs"]["addr"], "designer/project.json")

    if stale:
        print("\n备用 flash 变体与当前布局不一致（工程未使用，仅供参考）：\n")
        for s in stale:
            print("  " + s)
        print("\n  当前布局是 %dMB；这些是 flash_16M/8M/4M 旧变体，"
              "不随新规划更新。" % active_mb)

    if problems:
        print("\n发现 %d 处不一致：\n" % len(problems))
        for p in problems:
            print("  " + p)
        print("\n（上面只是「工程现值 vs layout.json」，哪边是对的由规划决定。）")
        return 1

    print("layout.json 与工程各处一致。")
    # also report the known _todo
    for sec in ("romfs", "fdb"):
        todo = lay.get(sec, {}).get("_todo")
        if todo:
            warn("%s: %s" % (sec, todo))
    return 0


# --------------------------------------------------------------------------
# app: merge + relocate + re-sign
# --------------------------------------------------------------------------

ROMFS_LAYOUT_H = os.path.join(APP_ROOT, "app", "designer", "src", "romfs_layout.h")
# Newer HoneyGUI Designer versions put user source files in src/user/, and romfs_layout.h moved with them.
ROMFS_LAYOUT_H_USER = os.path.join(APP_ROOT, "app", "designer", "src", "user",
                                   "romfs_layout.h")


def romfs_layout_paths():
    """返回需要同步的 romfs_layout.h 路径。

    Mi_eBadgeEntry.c 在 src/ 下，`#include "romfs_layout.h"` 会先搜 src/，
    所以两个副本不一致时 src/ 会遮蔽 src/user/ —— 都存在就必须一起写。
    都不存在时写到 designer 当前布局的位置（有 src/user/ 就写那里）。
    """
    existing = [p for p in (ROMFS_LAYOUT_H, ROMFS_LAYOUT_H_USER)
                if os.path.exists(p)]
    if existing:
        return existing
    if os.path.isdir(os.path.dirname(ROMFS_LAYOUT_H_USER)):
        return [ROMFS_LAYOUT_H_USER]
    return [ROMFS_LAYOUT_H]

ROMFS_LAYOUT_TMPL = """\
/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 * SPDX-License-Identifier: Apache-2.0
 *
 * Offset of romfs inside the app image.  Generated by tools/pack/pack.py;
 * do not edit by hand.
 *
 * The image base changes per bank (bank0 0x7009E000 / bank1 0x704F3000),
 * while app and romfs keep a fixed distance.  The mount uses
 * __rom_region_start + ROMFS_OFFSET, so both banks share one image.
 *
 * Regenerate with:
 *   python3 tools/pack/pack.py app --bank <bank> --app ... --romfs ... --out ...
 */
#ifndef _ROMFS_LAYOUT_H_
#define _ROMFS_LAYOUT_H_

/* Offset from the app image base, 64-byte aligned.
 * Generated for bank %(bank)s; romfs absolute address %(romfs_addr)s.
 * 0 = not packed yet: the mount points at app code (no UI resources). */
#define ROMFS_OFFSET %(offset)s

#endif /* _ROMFS_LAYOUT_H_ */
"""


def _version_rename_win(tmp, version_file):
    """Python 版 md5_generate.sh（仅 Windows 用；POSIX 仍走原脚本）。

    命名规则与脚本逐字段一致：
        <base>-v<MAJOR>.<MINOR>.<PATCH>.<TWEAK>-<GCID>-<MD5>.bin
    VERSION 前 6 行的空白分词顺序是
        VERSION_MAJOR = 0 / VERSION_MINOR = 0 / PATCHLEVEL = 0 /
        VERSION_TWEAK = 0 / EXTRAVERSION = x / VERSION_GCID = 0x...
    脚本用 awk -F'%' 取第 4/7/10/13/19 段，对应下面的 words[2/5/8/11/17]。
    返回新路径，失败返回 None。
    """
    try:
        with open(version_file, encoding="utf-8", errors="replace") as f:
            lines = f.read().splitlines()[:6]
    except OSError:
        return None
    words = []
    for line in lines:
        words.extend(line.split())
    if len(words) < 18:
        return None

    major, minor, rev, build = words[2], words[5], words[8], words[11]
    gcid = words[17][2:]          # equivalent to cut -c 3-

    h = hashlib.md5()
    with open(tmp, "rb") as f:
        for chunk in iter(lambda: f.read(8192), b""):
            h.update(chunk)

    out_dir = os.path.dirname(tmp)
    source_name = os.path.splitext(os.path.basename(tmp))[0]
    final = os.path.join(out_dir, "%s-v%s.%s.%s.%s-%s-%s.bin"
                         % (source_name, major, minor, rev, build, gcid,
                            h.hexdigest()))
    os.replace(tmp, final)
    return final


def _version_rename(path, base_name, keep_stale=False):
    """生成与原始 post_build **同名规则**的版本化产物。

    原始流程（cmake/post_build.cmake）最终产物：
        bin/<bank>/app_bank_<bank>_MP-v<M>.<m>.<r>.<b>-<GCID>-<MD5>.bin
    打包/烧录工具按 `<base>_MP-v*.bin` 扫描并只接受**唯一**匹配。

    所以这里必须：
      1. 用 base_name（如 app_bank0）而不是 out 文件名做前缀 ——
         否则多出的 `_packed` 让工具扫不到；
      2. 清掉同一目录下旧的 `<base>_MP-v*.bin`（那次编译产出的"未合并"
         版本）—— 否则两个文件同时匹配，工具可能取到没有 romfs 的那个。

    返回最终路径；失败返回 None。
    """
    script = os.path.join(GADGETS, "md5_generate.sh")
    version = os.path.join(APP_ROOT, "VERSION")
    if not os.path.exists(version):
        warn("缺少 VERSION，跳过版本化命名")
        return None
    if not IS_WIN and not os.path.exists(script):
        warn("缺少 md5_generate.sh，跳过版本化命名")
        return None

    out_dir = os.path.dirname(os.path.abspath(path))
    tmp = os.path.join(out_dir, base_name + "_MP.bin")
    # remove the previous round's intermediate name/old artifacts so glob does not match several
    for stale in glob.glob(os.path.join(out_dir, base_name + "_MP*.bin")):
        if os.path.abspath(stale) != os.path.abspath(path):
            os.unlink(stale)

    shutil.copy2(path, tmp)
    if IS_WIN:
        # On Windows the bash in PowerShell may be WSL (which cannot read C:\ paths),
        # so md5_generate.sh's naming rule is replicated directly in Python.
        if _version_rename_win(tmp, version) is None:
            warn("版本化命名失败（VERSION 格式异常），产物仍是 %s" % path)
            return None
    else:
        r = subprocess.run(["bash", script, tmp, version],
                           capture_output=True, text=True)
        if r.returncode != 0:
            warn("md5_generate.sh 失败：%s" % (r.stderr.strip() or r.returncode))
            return None

    pattern = base_name + "_MP-v*.bin"
    found = sorted(glob.glob(os.path.join(out_dir, pattern)))
    if not found:
        warn("未找到版本化产物（%s）" % pattern)
        return None
    final = found[-1]
    info("版本化产物: %s" % os.path.basename(final))
    if not keep_stale:
        others = [f for f in found if os.path.abspath(f) != os.path.abspath(final)]
        for o in others:
            os.unlink(o)
            info("移除同名旧产物: %s" % os.path.basename(o))
    return final


def _elf_flash_used(elf, app_addr, app_size):
    """ELF 在 app 区里的实际占用长度（字节，相对 app 基址）。拿不到返回 None。

    直接解析 ELF program header，不依赖 objdump —— 这层校验每次打包都要跑，
    而 objdump 在换台机器后未必找得到。

    有些段 vaddr 在 RAM/ITCM、paddr 才是 flash（Zephyr 的 code_relocation），
    所以两个都要看，取最大的那个末尾。区间用 app 分区卡死，否则 TRACE
    （0xe0640000）那种也 >= app 基址，会被算进来。
    """
    try:
        with open(elf, "rb") as f:
            blob = f.read(64)
    except OSError:
        return None
    if len(blob) < 52 or blob[:4] != b"\x7fELF" or blob[4] != 1 or blob[5] != 1:
        return None                       # only handle 32-bit little-endian (Cortex-M)
    phoff = struct.unpack_from("<I", blob, 0x1C)[0]
    entsize = struct.unpack_from("<H", blob, 0x2A)[0]
    phnum = struct.unpack_from("<H", blob, 0x2C)[0]
    if not entsize or not phnum:
        return None
    lo, hi = app_addr, app_addr + app_size
    used = 0
    try:
        with open(elf, "rb") as f:
            for i in range(phnum):
                f.seek(phoff + i * entsize)
                ph = f.read(32)
                if len(ph) < 32:
                    return None
                p_type = struct.unpack_from("<I", ph, 0)[0]
                _, p_vaddr, p_paddr, p_filesz = struct.unpack_from("<4I", ph, 4)
                if p_type != 1 or not p_filesz:      # PT_LOAD with file content
                    continue
                for base in (p_vaddr, p_paddr):
                    if lo <= base < hi:
                        used = max(used, base + p_filesz - app_addr)
    except OSError:
        return None
    return used or None


def _fw_elf_candidates(bank):
    """列出可能对应 bin/<bank>/app_<bank>.bin 的 ELF，按可信度排序。

    不要写死 ("build_<bank>_wifi", "build_<bank>") 这种固定顺序：snippet 组合
    各占一个 build 目录，固定顺序会挑到**没在用的那一个**。症状很隐蔽 ——
    no-wifi 编译的 fwcheck 永远读 build_<bank>_wifi/zephyr.elf 里的旧挂载地址，
    于是 -Pack 的收敛循环每轮都 touch + 重编、判 stale、再重编，直到轮数用尽报错。
    """
    out = []
    # 1) the copy post-build placed in bin/<bank>/, produced together with app_<bank>.bin
    p = os.path.join(APP_ROOT, "bin", bank, "app_%s.elf" % bank)
    if os.path.exists(p):
        out.append(p)
    # 2) the build directories of the snippet combinations, newest first
    builds = []
    for d in glob.glob(os.path.join(APP_ROOT, "build_%s*" % bank)):
        e = os.path.join(d, "zephyr", "zephyr.elf")
        if os.path.isfile(e):
            builds.append((os.path.getmtime(e), e))
    out.extend(e for _, e in sorted(builds, reverse=True))
    return out


def _fw_mount_check(bank, app_addr, offset, elf=None):
    """检查固件 ELF 是否真的会把 romfs 挂到 app_addr + offset。

    固件写成 `__rom_region_start + ROMFS_OFFSET`，而 __rom_region_start 是
    linker 绝对符号，编译后 app_init 的字面量池里就是那个绝对地址。所以
    "ELF 里出现这个值"等价于"固件会挂到正确位置"。

    返回 (状态, 说明)，状态 ∈ {"ok", "stale", "skip"}。
    找不到 ELF / objdump 时返回 skip —— 环境缺失不该阻断打包。
    """
    if elf is None:
        cands = _fw_elf_candidates(bank)
        elf = cands[0] if cands else None
    if elf is None:
        return "skip", "未找到 %s 的编译产物" % bank

    tool = os.environ.get("OBJDUMP")
    if tool is None:
        cand = os.path.join(os.environ.get("ZEPHYR_SDK_INSTALL_DIR", ""),
                            "arm-zephyr-eabi", "bin", "arm-zephyr-eabi-objdump")
        if IS_WIN and os.path.exists(cand + ".exe"):
            cand += ".exe"
        tool = cand if os.path.exists(cand) else "arm-zephyr-eabi-objdump"
    try:
        r = subprocess.run([tool, "-d", elf], capture_output=True, text=True)
    except (FileNotFoundError, OSError):
        return "skip", "找不到 objdump（设 OBJDUMP=... 可启用）"
    if r.returncode != 0:
        return "skip", "objdump 执行失败"

    # The literal pool sits at the end of the function, after the bl, so searching only this code slice is not enough.
    # Correct approach: take the address each ldr [pc, #N] before a bl points to, then look up those
    # addresses' .word values in the whole output — one of them is the mount address.
    #
    # Note that app_init may call gui_vfs_mount_romfs **several** times: Designer-generated code
    # first mounts a hardcoded old address, then unmounts and switches to __rom_region_start + ROMFS_OFFSET.
    # Checking only the first would wrongly report stale (the first constant is naturally the old address), so check every one.
    calls = list(re.finditer(r"bl\s+\w+\s+<gui_vfs_mount_romfs>", r.stdout))
    if not calls:
        return "skip", "app_init 里找不到 gui_vfs_mount_romfs 调用"

    want = app_addr + offset
    pool = {int(a, 16): int(w, 16) for a, w in
            re.findall(r"^\s*([0-9a-f]+):\s+[0-9a-f]+\s+\.word\s+(0x[0-9a-fA-F]+)",
                       r.stdout, re.M)}
    saw_ref = False
    for c in calls:
        seg = r.stdout[max(0, c.start() - 512):c.start()]
        refs = {int(a, 16) for a in
                re.findall(r"ldr\s+\w+,\s*\[pc,\s*#-?\d+\]\s*;\s*\(([0-9a-f]+)",
                           seg)}
        if not refs:
            continue
        saw_ref = True
        for addr in refs:
            if pool.get(addr) == want:
                return "ok", elf
    if not saw_ref:
        return "skip", "app_init 里没有可解析的 ldr 字面量"
    return "stale", elf


def _write_romfs_layout(offset, bank, romfs_addr, no_update):
    text = ROMFS_LAYOUT_TMPL % dict(offset=hexs(offset), bank=bank,
                                    romfs_addr=hexs(romfs_addr))
    if no_update:
        info("ROMFS_OFFSET = %s（--no-layout-update：未写入 romfs_layout.h）"
             % hexs(offset))
        return

    changed = []
    for target in romfs_layout_paths():
        old = None
        if os.path.exists(target):
            with open(target, encoding="utf-8") as f:
                old = f.read()
        if old == text:
            continue
        with open(target, "w", encoding="utf-8") as f:
            f.write(text)
        changed.append(os.path.relpath(target, APP_ROOT))
    if not changed:
        info("romfs_layout.h 已是 ROMFS_OFFSET=%s" % hexs(offset))
        return
    for rel in changed:
        info("已更新 %s: ROMFS_OFFSET = %s" % (rel, hexs(offset)))
    warn("固件需要重新编译才生效（增量编译即可）")


def cmd_fwcheck(args):
    """给编译脚本用：检查 ELF 里的 romfs 挂载地址是否已是本次偏移。

    返回码：0 = OK（或无法校验，如缺 ELF/objdump），2 = ELF 里还是旧地址。
    编译脚本据此决定是否需要 touch 源文件后增量重编。
    """
    lay, _ = load_layout(args.layout)
    if args.bank not in lay["banks"]:
        die("未知 bank: %s" % args.bank)
    app_addr = lay["banks"][args.bank]["app_addr"]
    state, detail = _fw_mount_check(args.bank, app_addr, args.offset, args.elf)
    print("fwcheck: %s (%s)" % (state, detail))
    return 2 if state == "stale" else 0


def cmd_app(args):
    lay, _ = load_layout(args.layout)
    bank = args.bank
    if bank not in lay["banks"]:
        die("未知 bank: %s" % bank)
    b = lay["banks"][bank]

    app_addr = b["app_addr"]
    app_size = b["app_size"]

    with open(args.app, "rb") as f:
        app = bytearray(f.read())
    with open(args.romfs, "rb") as f:
        romfs = bytearray(f.read())

    hdr = read_img_header(app)
    if hdr["payload_len"] + HDR_SIZE != len(app):
        die("app 镜像长度与头里的 payload_len 不一致（0x%X+0x%X != 0x%X）"
            % (hdr["payload_len"], HDR_SIZE, len(app)))
    info("app: %s  payload=%s  img_id=0x%04X"
         % (os.path.basename(args.app), hexs(hdr["payload_len"]), hdr["img_id"]))

    # The ELF post-build copied next to this bin — produced together with the image, so there is no
    # "wrong build directory" problem. It is used by the "romfs must not overlap code" check below and by the final fwcheck.
    elf = os.path.splitext(args.app)[0] + ".elf"
    if not os.path.exists(elf):
        elf = None

    # romfs target address.
    #
    # The default is "right after the end of app" (64-aligned). This is not arbitrary: the app partition is only 1.41MB,
    # and between the end of app and the old romfs address (0x70536400) lie 3.6MB of blank space — placing
    # romfs at that old offset makes 3.6MB of pure padding in the merged image, larger than the partition itself,
    # so it would never pass the size check. Placed right after, the merged length is ≈ app + romfs.
    #
    # The cost: the absolute romfs address varies with the size of app and also differs between bank0/bank1 bases.
    # So the firmware cannot mount with a compile-time constant and must use a run-time expression:
    #     gui_vfs_mount_romfs("/", (void *)ROMS_PTR(), 0);
    #     #define ROMFS_OFFSET 0x00100000   /* output by pack.py */
    #     static inline void *ROMS_PTR(void)
    #     { extern unsigned char __rom_region_start[];   /* linker: app base */
    #       return __rom_region_start + ROMFS_OFFSET; }
    # This tool prints the ROMFS_OFFSET to fill in when it finishes.
    if args.romfs_addr is not None:
        romfs_addr = args.romfs_addr
        romfs_off = romfs_addr - app_addr
    elif args.romfs_offset is not None:
        romfs_off = args.romfs_offset
        romfs_addr = app_addr + romfs_off
    else:
        # default target: right after app, aligned up to the flash erase block (4KB).
        #
        # Why 4KB instead of the 64B romfs requires internally: this way romfs occupies whole
        # erase sectors and app's last sector is not shared with romfs. The capability this gives is that —
        # after changing wallpaper you can **flash only romfs** (-A <mount address> -F romfs.bin),
        # without reflashing the whole 2.5MB merged image; otherwise erasing romfs' first sector would erase the tail of app.
        romfs_off = (len(app) + args.romfs_gap + args.romfs_align - 1) \
                    // args.romfs_align * args.romfs_align
        romfs_addr = app_addr + romfs_off
    if romfs_off <= 0:
        die("romfs 偏移必须为正（got %s）" % hexs(romfs_off))
    if romfs_off % 64 != 0:
        die("romfs 偏移 %s 必须 64 字节对齐（romfs 文件内部按 64 对齐布局）"
            % hexs(romfs_off))
    if args.romfs_align > 64 and romfs_addr % args.romfs_align != 0:
        # --romfs-offset/--romfs-addr given explicitly but not aligned: warn instead of failing,
        # because someone may need to reproduce a historical layout; but standalone romfs flashing is then unsafe.
        warn("romfs 绝对地址 %s 未按 %d 字节对齐：单独烧 romfs 会擦掉"
             "app 的尾部（该扇区与 app 共用）" % (hexs(romfs_addr),
                                                args.romfs_align))
    # Hard check: romfs must not overlap app code.
    #
    # Looking only at len(app) is not enough — what decides "where the code ends" is the firmware ELF. If the two are not from the
    # same build (which is what happens after switching snippet combinations and only building, not packing), len(app) is too small,
    # romfs lands right in the code region, flashing overwrites the tail of app and the result looks like an all-black UI or a crash,
    # which is hard to trace back to "the offset was computed too small". So the ELF's actual usage is checked as well.
    used = len(app)
    elf_used = _elf_flash_used(elf, app_addr, app_size) if elf else None
    if elf_used is not None:
        if elf_used != len(app):
            warn("%s 的 flash 占用（%s）与 %s 的长度（%s）不一致 —— "
                 "多半是两者不是同一次编译"
                 % (os.path.basename(elf), hexs(elf_used),
                    os.path.basename(args.app), hexs(len(app))))
        used = max(used, elf_used)
    if romfs_off < used:
        die("romfs 会压到 app 代码上，拒绝打包：\n"
            "       app  占用 %s，代码末尾在 %s\n"
            "       romfs 落点 %s，起于 %s\n"
            "       重叠 %s 字节 —— 烧进去就是 app 尾部被 romfs 覆盖（UI 全黑 / 跑飞）\n"
            "   原因：ROMFS_OFFSET 是按另一套编译参数算出来的。带 wifi 和不带 wifi\n"
            "         的 app 差约 34KB，但 bin/<bank>/ 和 romfs_layout.h 两种组合共用一份。\n"
            "   处理：用你实际要烧的那套参数重跑一遍，让它按新尺寸重算偏移并重编收敛：\n"
            "       Windows : .\\tools\\scripts\\build.ps1 -Bank <N> [-Snippet wifi] -Pack\n"
            "       WSL     : ./tools/scripts/build.sh --bank <N> [--snippet wifi] --pack\n"
            "   （别只 build 不 pack —— 重算偏移和 touch 入口文件重编都在 -Pack/--pack 里）"
            % (hexs(used), hexs(app_addr + used),
               hexs(romfs_off), hexs(romfs_addr),
               hexs(used - romfs_off)))
    gap = romfs_off - len(app)
    info("romfs @ %s（app 基址 + %s）；app 末尾到 romfs 之间 %s 字节空隙，"
         "会计入合并镜像长度" % (hexs(romfs_addr), hexs(romfs_off), hexs(gap)))
    if gap > (len(romfs) + 0x10000):
        warn("空隙 %s 比 romfs 本身（%s）还大：合并镜像大部分是填充。"
             "调小 romfs 偏移能同时省 OTA 传输量和 flash 占用。"
             % (hexs(gap), hexs(len(romfs))))

    # relocate romfs to its final address
    old_base, new_base, n_ent = romfs_relocate(romfs, romfs_addr,
                                               args.romfs_old_base)
    info("romfs: 重定位 %s -> %s（%d 个目录项）"
         % (hexs(old_base), hexs(new_base), n_ent))
    if old_base != romfs_addr:
        info("注意：固件挂载地址必须改成 %s（原 %s）"
             % (hexs(romfs_addr), hexs(old_base)))

    merged = bytearray(b"\xff" * (romfs_off + len(romfs)))
    merged[0:len(app)] = app
    merged[romfs_off:romfs_off + len(romfs)] = romfs

    merged_len = len(merged)
    info("合并后 %s（app %s + 空隙 %s + romfs %s）"
         % (hexs(merged_len), hexs(len(app)),
            hexs(romfs_off - len(app)), hexs(len(romfs))))

    # size check: OTA uses the slot size in the OTA header as the upper bound
    if merged_len > app_size:
        msg = ("合并镜像 %s 超过 app 分区 %s（差 %s）。OTA 时 "
               "app_ota_check_section_size 会直接拒绝。"
               % (hexs(merged_len), hexs(app_size),
                  hexs(merged_len - app_size)))
        if args.allow_oversize:
            warn(msg)
        else:
            die(msg + "\n     若确实要产出（例如先看体积），加 --allow-oversize。")

    out = args.out
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    tmp = out + ".tmp"
    with open(tmp, "wb") as f:
        f.write(merged)

    # 1) update payload_len (+ optional re-sign)
    cmd = [gadget("prepend_header"), "/app_code", tmp,
           "/ic_type", args.ic_type]
    if args.sign:
        cmd += ["/ecdsa", os.path.join(GADGETS, "ecdsa_key.pem")]
    if args.verbose:
        info("$ " + " ".join(cmd))
    r = run(cmd, capture_output=True, text=True, verbose=False)
    if r.returncode != 0:
        die("prepend_header 失败:\n%s%s" % (r.stdout, r.stderr))
    info(r.stdout.strip().splitlines()[-1] if r.stdout.strip() else "image header ok")

    # 2) optional MP header (the tool writes the result as <name>_MP.bin)
    if args.mp:
        base = os.path.splitext(tmp)[0]
        mp_cmd = [gadget("prepend_header"),
                  "/app_code", tmp, "/mp_ini", args.mp_ini,
                  "/ic_type", args.ic_type]
        if args.verbose:
            info("$ " + " ".join(mp_cmd))
        r = run(mp_cmd, capture_output=True, text=True, verbose=False)
        if r.returncode != 0:
            die("MP 头生成失败:\n%s%s" % (r.stdout, r.stderr))
        mp_out = base + "_MP.bin"
        if not os.path.exists(mp_out):
            die("未生成 MP 镜像: %s" % mp_out)
        shutil.move(mp_out, out)
        info("已写入 %s（含 MP 头）" % out)
    else:
        shutil.move(tmp, out)
        info("已写入 %s" % out)
    if os.path.exists(tmp):
        os.unlink(tmp)

    # 2.5) export a "standalone flashable" romfs.
    #
    # The romfs target is 4K aligned, so it shares no erase sector with app — after changing wallpaper you can flash only
    # this region, saving the transfer time of the whole merged image. But it must be the **relocated** copy:
    # pointers in the original app_romfs_*.bin still point at the old base it had when mkromfs ran, and flashing
    # it directly makes every directory entry point to the wrong place.
    if args.romfs_out:
        with open(args.romfs_out, "wb") as f:
            f.write(romfs)
        info("可单烧 romfs: %s (%d B, 落点 %s)"
             % (args.romfs_out, len(romfs), hexs(romfs_addr)))

    # 3) sync the firmware's ROMFS_OFFSET (fw mounts with __rom_region_start + it)
    _write_romfs_layout(romfs_off, bank, romfs_addr, args.no_layout_update)

    # 3.5) versioned file name — exactly the same as the original post_build:
    #      after adding the MP header, cmake/post_build.cmake calls
    #        bash Gadgets/md5_generate.sh <xxx_MP.bin> <VERSION>
    #      (Windows has no reliable bash, so _version_rename_win() replicates it in Python)
    #      and renames xxx_MP.bin to
    #        xxx_MP-v<MAJOR>.<MINOR>.<REV>.<BUILD>-<GCID>-<MD5>.bin
    #      The pack/flash directories depend on this name (the md5 is also used to verify image uniqueness).
    #      Do the same to the final artifact here, while keeping a fixed name for local debugging.
    if args.version_name:
        # the base name must match the original post_build: app_bank<N>, without _packed
        _version_rename(out, "app_%s" % bank)

    # 4) artifact self-check: re-parse the header + the relocated romfs pointers
    with open(out, "rb") as f:
        chk = f.read()
    off = MP_HDR_SIZE if (chk[:2] == b"\x01\x00" and chk[:6] != b"IMGHDR") else 0
    body = chk[off:]
    h2 = read_img_header(body)
    want_payload = merged_len - HDR_SIZE
    if h2["payload_len"] != want_payload:
        die("自检失败：payload_len=%s 期望 %s"
            % (hexs(h2["payload_len"]), hexs(want_payload)))
    rb = bytearray(body[romfs_off:romfs_off + len(romfs)])
    base2, rows = romfs_list(rb)
    if base2 != romfs_addr:
        die("自检失败：romfs 基址 %s != %s" % (hexs(base2), hexs(romfs_addr)))
    info("自检通过：payload_len=%s, romfs@%s（%d 项）"
         % (hexs(h2["payload_len"]), hexs(base2), len(rows)))

    # 5) mount address traceability: print both "what the firmware should read" and "what it has now".
    #    Changing the romfs target changes only this number, but both sides must stay in sync or boot mounts a wild address.
    print("")
    print("  romfs 挂载：")
    print("    偏移   ROMFS_OFFSET = %s   (相对 app 基址，两个 bank 共用)" % hexs(romfs_off))
    print("    绝对   %s: %s" % (bank, hexs(romfs_addr)))
    for bk, bv in lay["banks"].items():
        print("            %s: %s" % (bk, hexs(bv["app_addr"] + romfs_off)))
    print("    固件    app/designer/src[/user]/romfs_layout.h + __rom_region_start")

    st, detail = _fw_mount_check(bank, app_addr, romfs_off, elf)
    if st == "ok":
        print("    校验    ELF 里的挂载地址一致 ✓")
    elif st == "stale":
        warn("ELF 里的挂载地址与本次偏移不一致 —— 固件还是旧的，"
             "需要增量重编（build.sh --pack 会自动重试）")
    else:
        info("跳过 ELF 校验：%s" % detail)
    print("")
    return 0


# --------------------------------------------------------------------------
# fdb: generate the FlashDB image on the host
# --------------------------------------------------------------------------

FAL_CFG_TMPL = """\
#ifndef _FAL_CFG_H_
#define _FAL_CFG_H_
#include <fal_def.h>
#define FAL_FLASH_DEV_TABLE {{ &nor_flash_host }}
#define FAL_PART_HAS_TABLE_CFG
/* 生成镜像内部坐标：kvdb 在 0，bf 紧随其后；整体烧到 kvdb 绝对地址 */
#define FAL_PART_TABLE \\
{{ {{0x45503130, "fdb_kvdb1", "norhost", 0, {kvdb}, 0}}, \\
   {{0x45503130, "bf_data",   "norhost", {kvdb}, {bf}, 0}} }}
extern struct fal_flash_dev nor_flash_host;
#endif
"""

FDB_SRC = ["src/fdb.c", "src/fdb_kvdb.c", "src/fdb_utils.c", "src/bf/fdb_bf.c",
           "port/fal/src/fal.c", "port/fal/src/fal_flash.c",
           "port/fal/src/fal_partition.c"]


def _check_fdb_cfg_parity():
    """host 与固件的 fdb_cfg.h 功能宏必须一致（决定 on-flash 布局）。"""
    def macros(path):
        with open(path, encoding="utf-8", errors="replace") as f:
            t = f.read()
        return {m.group(1) for m in
                re.finditer(r"^\s*#\s*define\s+(\w+)", t, re.M)
                if m.group(1).startswith("FDB_USING") or m.group(1) == "FDB_WRITE_GRAN"}

    fw = macros(os.path.join(APP_ROOT, "port", "flashdb", "fdb_cfg.h"))
    hc = macros(os.path.join(HERE, "fdb_host", "fdb_cfg.h"))
    if fw != hc:
        die("fdb_cfg.h 与固件不一致：固件多 %s，host 多 %s。"
            "两边必须一致，否则产出的 on-flash 布局不同。"
            % (sorted(fw - hc), sorted(hc - fw)))


def _build_host_tools(build_dir, kvdb, bf):
    """编译两个 host 工具：生成器 + 校验器，共用同一批 FlashDB 目标文件。"""
    os.makedirs(build_dir, exist_ok=True)
    with open(os.path.join(build_dir, "fal_cfg.h"), "w", encoding="utf-8") as f:
        f.write(FAL_CFG_TMPL.format(kvdb=kvdb, bf=bf))

    cc = host_cc()
    exe_suffix = ".exe" if IS_WIN else ""
    inc = ["-I" + os.path.join(build_dir),                 # generated/copied config header
           "-I" + os.path.join(HERE, "fdb_host"),          # fdb_cfg.h
           "-I" + os.path.join(FLASHDB, "inc"),
           "-I" + os.path.join(FLASHDB, "port", "fal", "inc")]
    objs = []
    for rel in FDB_SRC:
        src = os.path.join(FLASHDB, rel)
        obj = os.path.join(build_dir, rel.replace("/", "_") + ".o")
        r = run([cc, "-O1", "-g", "-c", src, "-o", obj] + inc,
                capture_output=True, text=True, verbose=False)
        if r.returncode != 0:
            die("编译 FlashDB 失败 (%s):\n%s" % (rel, r.stderr))
        objs.append(obj)

    exes = {}
    for name in ("fdb_host_pack", "fdb_host_verify"):
        src = os.path.join(HERE, "fdb_host", name + ".c")
        exe = os.path.join(build_dir, name + exe_suffix)
        r = run([cc, "-O1", "-g", src, "-o", exe] + objs + inc,
                capture_output=True, text=True, verbose=False)
        if r.returncode != 0:
            die("链接 %s 失败:\n%s" % (name, r.stderr))
        exes[name] = exe
    return exes


def _collect_assets(dirpath, with_video=False):
    """按命名规则收集 eBadge 资源，返回 (files, skipped)。

    命名与固件侧约定一致（BF key 由写入路径的 pick_file_id 生成 wp<N>，
    这里沿用同一形状，并让 pre/video 也带上自己的前缀以便固件区分）：

        wp-1.bin   -> wp1        壁纸
        pre-1.bin  -> pre1       缩略图（对应 wp1）
        xxx.avi    -> videoN     视频

    跳过 AVI 时不报错，只在 skipped 里说明原因。
    """
    pat = re.compile(r"^(wp|pre)-(\d+)\.bin$", re.I)
    walls, previews, videos, skipped = {}, {}, [], []
    found = []
    for name in sorted(os.listdir(dirpath)):
        path = os.path.join(dirpath, name)
        if not os.path.isfile(path):
            continue
        if name.lower().endswith(".avi"):
            videos.append((name, path))
            continue
        m = pat.match(name)
        if m:
            n = int(m.group(2))
            (walls if m.group(1).lower() == "wp" else previews)[n] = path
            continue
        skipped.append("%s（不符合 wp-N.bin / pre-N.bin 命名）" % name)

    # wallpapers and previews are paired by index into one group: wp1=<wp-1>,pre=<pre-1>
    for n in sorted(walls):
        spec = "wp%d=%s" % (n, walls[n])
        if n in previews:
            spec += ",pre=%s" % previews[n]
        found.append(("wp%d" % n, spec))
    for n in sorted(previews):
        if n not in walls:
            skipped.append("pre-%d.bin（没有对应的 wp-%d.bin）" % (n, n))

    for i, (name, path) in enumerate(videos, 1):
        if with_video:
            found.append(("video%d" % i, "video%d=%s" % (i, path)))
        else:
            skipped.append("%s（视频默认跳过，用 --with-video 加入）" % name)
    return found, skipped


def cmd_fdb(args):
    lay, _ = load_layout(args.layout)
    fdb = lay["fdb"]
    _check_fdb_cfg_parity()

    files = []
    for spec in args.file or []:
        if "=" not in spec:
            die("--file 需要 <key>=<path>[,pre=..][,mask=..][,part=..]，收到 '%s'" % spec)
        k, rest = spec.split("=", 1)
        for part in rest.split(","):
            val = part.split("=", 1)[-1]        # accept "data=p" and a bare "p"
            if not os.path.exists(val):
                die("资源不存在: %s" % val)
        files.append((k, spec))

    # --assets-dir: bulk-collect by eBadge's asset naming rule
    #   wp-N.bin   -> key "wpN"    wallpaper (SRC_IMG)
    #   pre-N.bin  -> key "preN"   preview   (img_preview)
    #   *.avi      -> key "videoN" video     (SRC_VIDEO, skipped by default)
    # Other files in the directory (e.g. bins not following the convention) are ignored and reported.
    if args.assets_dir:
        if not os.path.isdir(args.assets_dir):
            die("--assets-dir 不是目录: %s" % args.assets_dir)
        found, skipped = _collect_assets(args.assets_dir, args.with_video)
        files.extend(found)
        for s in skipped:
            info("跳过 %s" % s)

    if not files and not args.empty:
        die("没有 --file/--assets-dir，也没有 --empty")

    build_dir = args.build_dir or os.path.join(APP_ROOT, "build_pack", "fdb_host")
    exes = _build_host_tools(build_dir, fdb["kvdb_size"], fdb["bf_data_size"])

    out = args.out
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    cmd = [exes["fdb_host_pack"], "--out", out,
           "--xip-base", hex(fdb["kvdb_addr"]),
           "--kvdb-size", str(fdb["kvdb_size"]),
           "--bf-size", str(fdb["bf_data_size"]),
           "--crc-skip", str(args.crc_skip)]
    if args.no_wrap:
        cmd.append("--no-wrap")
    if args.full:
        cmd.append("--full")
    if args.img_size:
        cmd += ["--img-size", args.img_size]
    for k, spec in files:
        cmd += ["--file", spec]
    r = run(cmd, capture_output=False, verbose=args.verbose)
    if r.returncode != 0:
        die("fdb_host_pack 失败 rc=%d" % r.returncode)

    # Read back immediately: simulate the device booting through kvdb_init -> bf_init -> bf_foreach.
    # This catches a whole class of "written fine but unreadable" problems (and verifies nothing was auto-formatted).
    vcmd = [exes["fdb_host_verify"], "--img", out,
            "--xip-base", hex(fdb["kvdb_addr"]),
            "--kvdb-size", str(fdb["kvdb_size"]),
            "--bf-size", str(fdb["bf_data_size"])]
    for k, _ in files:
        vcmd += ["--key", k]
    info("读回校验：")
    r = run(vcmd, capture_output=False, verbose=args.verbose)
    if r.returncode != 0:
        die("读回校验失败 rc=%d —— 镜像不能被固件识别，不要烧录"
            % r.returncode)

    kvdb_addr = fdb["kvdb_addr"]
    if fdb["bf_data_addr"] != kvdb_addr + fdb["kvdb_size"]:
        die("layout 里 bf_data_addr 必须紧接 kvdb（%s + %s = %s，实际 %s）；"
            "生成镜像把两个分区当作连续段输出"
            % (hexs(kvdb_addr), hexs(fdb["kvdb_size"]),
               hexs(kvdb_addr + fdb["kvdb_size"]), hexs(fdb["bf_data_addr"])))
    actual = os.path.getsize(out)
    total = fdb["kvdb_size"] + fdb["bf_data_size"]
    info("FDB 镜像已生成：%s（%d B%s）"
         % (out, actual,
            "，尾部 %.1f MB 未写入（保持擦除态）" % ((total - actual) / 1048576.0)
            if actual < total else ""))
    info("烧录: -A %s -F %s （区域内 kvdb %s..%s，bf %s..%s）"
         % (hexs(kvdb_addr), out, hexs(kvdb_addr),
            hexs(kvdb_addr + fdb["kvdb_size"]),
            hexs(fdb["bf_data_addr"]),
            hexs(fdb["bf_data_addr"] + fdb["bf_data_size"])))
    return 0


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------

def main():
    p = argparse.ArgumentParser(description="eBadge 打包工具")
    p.add_argument("--layout", help="布局文件（默认 tools/pack/layout.json）")
    sub = p.add_subparsers(dest="cmd", required=True)

    c = sub.add_parser("check", help="校验布局与工程各处是否一致")
    c.set_defaults(func=cmd_check)

    w = sub.add_parser("fwcheck",
                       help="校验 ELF 里的 romfs 挂载地址是否为当前偏移")
    w.add_argument("--bank", required=True, choices=["bank0", "bank1"])
    w.add_argument("--offset", type=lambda s: int(s, 0), required=True)
    w.add_argument("--elf",
                   help="要检查的 ELF（默认 bin/<bank>/app_<bank>.elf，"
                        "其次各 build 目录里最新的那份）")
    w.set_defaults(func=cmd_fwcheck)

    a = sub.add_parser("app", help="app + romfs 合并为一个镜像")
    a.add_argument("--bank", required=True, choices=["bank0", "bank1"])
    a.add_argument("--app", required=True,
                   help="bin/<bank>/app_<bank>.bin（带镜像头，无 MP 头）")
    a.add_argument("--romfs", required=True, help="app_romfs_*.bin")
    a.add_argument("--out", required=True)
    a.add_argument("--romfs-offset", type=lambda s: int(s, 0), metavar="OFF",
                   help="romfs 相对 app 基址的固定偏移（不填则紧跟 app 末尾）")
    a.add_argument("--romfs-addr", type=lambda s: int(s, 0),
                   help="直接给 romfs 绝对地址（优先于其它）")
    a.add_argument("--romfs-gap", type=int, default=0, metavar="BYTES",
                   help="紧跟 app 时额外留的空隙（默认 0）")
    a.add_argument("--romfs-out", metavar="FILE",
                   help="额外导出一份已重定位的 romfs，可单独烧到挂载地址")
    a.add_argument("--romfs-align", type=lambda s: int(s, 0), default=4096,
                   metavar="BYTES",
                   help="romfs 落点对齐到该值（默认 4096 = flash 擦除块）。"
                        "对齐后 romfs 独占完整扇区，可单独烧录")
    a.add_argument("--romfs-old-base", type=lambda s: int(s, 0),
                   help="romfs 原始基址（默认从目录项反推）")
    a.add_argument("--ic-type", default="8773G")
    a.add_argument("--sign", action="store_true", help="重新做 ECDSA 签名")
    a.add_argument("--mp", action="store_true", help="追加 MP 头（输出 <out> 含 MP）")
    a.add_argument("--mp-ini", default=os.path.join(APP_ROOT, "mp.ini"))
    a.add_argument("--allow-oversize", action="store_true")
    a.add_argument("--version-name", action="store_true", default=True,
                   help="额外生成 md5_generate.sh 风格的版本化文件名（默认开）")
    a.add_argument("--no-version-name", dest="version_name",
                   action="store_false")
    a.add_argument("--no-layout-update", action="store_true",
                   help="不改写 app/designer/src[/user]/romfs_layout.h")
    a.add_argument("--verbose", action="store_true")
    a.set_defaults(func=cmd_app)

    d = sub.add_parser("fdb", help="生成预制资源的 FDB 镜像")
    d.add_argument("--out", required=True)
    d.add_argument("--file", action="append", metavar="KEY=PATH",
                   help="BF 文件名(key) 与资源路径，可多次")
    d.add_argument("--assets-dir", metavar="DIR",
                   help="按命名规则批量收集：wp-N.bin / pre-N.bin / *.avi")
    d.add_argument("--with-video", action="store_true",
                   help="把 --assets-dir 里的 .avi 也打进来（默认跳过）")
    d.add_argument("--full", action="store_true",
                   help="产出与 BF 分区等长的完整镜像（默认只写到用到的末尾）")
    d.add_argument("--no-wrap", action="store_true",
                   help="不封装 EBXF+GUI 头（固件读不出图片，仅调试用）")
    d.add_argument("--img-size", metavar="WxH",
                   help="GUI 头里的宽高（默认 466x466）")
    d.add_argument("--crc-skip", type=lambda s: int(s, 0), default=80,
                   help="目录项 CRC 跳过前 N 字节（默认 80=EBXF40+索引头40）")
    d.add_argument("--empty", action="store_true", help="允许没有任何文件")
    d.add_argument("--build-dir")
    d.add_argument("--verbose", action="store_true")
    d.set_defaults(func=cmd_fdb)

    args = p.parse_args()
    sys.exit(args.func(args))


if __name__ == "__main__":
    main()
