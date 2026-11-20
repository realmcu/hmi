#!/usr/bin/env bash
# Flash only romfs (wallpaper/UI assets) without touching app — a quick verification path after changing assets.
#
# Why flashing only this region works: build.sh --pack places romfs at a 4KB-aligned address,
# so it shares no flash erase sector with app. Flashing romfs therefore erases only
# [romfs start rounded down to 4KB, romfs end rounded up], leaving app code completely untouched.
#
# Prerequisites:
#   1. ./tools/scripts/build.sh --bank <N> --pack has already been run
#      (producing bin/bank<N>/app_romfs_bank<N>_reloc.bin)
#   2. the directory-entry pointers in that reloc file already point at this mount address
#      — using app/designer/build/app_romfs_*.bin directly does not work: its pointers still hold
#         the old absolute address, and flashing it turns the whole UI black.
#
# Under WSL the Windows mpcli.exe must be used: COM ports are Windows devices, so the
# Linux mpcli cannot open them from WSL. This follows the two paths of scripts/flash-wsl-app.sh:
#   1) WSLInterop registered -> run the .exe directly
#   2) not registered        -> /init + temp .bat
#
# Usage:
#   ./tools/scripts/flash_romfs.sh                    # default bank0 + COM5
#   ./tools/scripts/flash_romfs.sh --bank 1
#   ./tools/scripts/flash_romfs.sh --port COM7
#   DRY=1 ./tools/scripts/flash_romfs.sh              # print the command only
#
# The image address is derived from these two places; it is not hardcoded in this script:
#   APP base     ← BANK<N>_APP_ADDR in flash_map.ini
#   ROMFS offset ← ROMFS_OFFSET in app/designer/src[/user]/romfs_layout.h
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"                 # application project root = repo root

MPCLI_EXE="${MPCLI_EXE:-$APP_DIR/scripts/tool/mpcli/mpcli.exe}"
FLASH_MAP="$APP_DIR/flash_map.ini"

# romfs_layout.h may live in src/ or src/user/ (the new Designer export layout).
LAYOUT_H="$APP_DIR/app/designer/src/romfs_layout.h"
if [ ! -f "$LAYOUT_H" ] && [ -f "$APP_DIR/app/designer/src/user/romfs_layout.h" ]; then
    LAYOUT_H="$APP_DIR/app/designer/src/user/romfs_layout.h"
fi

BANK=0
PORT="${PORT:-COM5}"
BAUD="${BAUD:-3000000}"
DRY="${DRY:-0}"

usage() {
    cat <<EOF
用法: $(basename "$0") [--bank 0|1] [--port COM<n>] [--baud <n>] [--dry-run]

只烧 romfs（壁纸/UI 资源）到已烧好 app 的设备，省去重烧整个镜像。
需要先执行: ./tools/scripts/build.sh --bank <N> --pack

环境变量: MPCLI_EXE PORT BAUD DRY=1
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --bank) BANK="$2"; shift 2 ;;
        --port) PORT="$2"; shift 2 ;;
        --baud) BAUD="$2"; shift 2 ;;
        --dry-run) DRY=1; shift ;;
        -h|--help) usage; exit 0 ;;
        -*) echo "未知参数: $1" >&2; usage; exit 1 ;;
        *)  PORT="$1"; shift ;;          # positional argument = port
    esac
done

case "$BANK" in
    0|1) ;;
    *) echo "错误: --bank 只能是 0 或 1" >&2; exit 1 ;;
esac

ROMFS="$APP_DIR/bin/bank${BANK}/app_romfs_bank${BANK}_reloc.bin"

die() { echo "[romfs] 错误: $*" >&2; exit 1; }

# ---- preflight checks ----
[ -f "$MPCLI_EXE" ] || die "找不到 mpcli.exe: $MPCLI_EXE"
[ -f "$ROMFS" ] || die "找不到 $ROMFS
       先执行: ./tools/scripts/build.sh --bank $BANK --pack"
[ -f "$FLASH_MAP" ] || die "找不到 flash_map.ini"
[ -f "$LAYOUT_H" ] || die "找不到 romfs_layout.h"

# offset: ROMFS_OFFSET 0x000F2000
OFFSET_HEX=$(grep -oE '#define[[:space:]]+ROMFS_OFFSET[[:space:]]+(0x[0-9A-Fa-f]+)' "$LAYOUT_H" \
             | grep -oE '0x[0-9A-Fa-f]+' | head -1)
[ -n "$OFFSET_HEX" ] || die "romfs_layout.h 里没有 ROMFS_OFFSET"

# app base: BANK0_APP_ADDR=0x7009E000
APP_ADDR_HEX=$(grep -E "^BANK${BANK}_APP_ADDR=" "$FLASH_MAP" | cut -d= -f2 | tr -d '[:space:]')
[ -n "$APP_ADDR_HEX" ] || die "flash_map.ini 里没有 BANK${BANK}_APP_ADDR"

# ---- verify the image (4K alignment / pointers / directory structure) ----
MOUNT_HEX=$(python3 - "$APP_ADDR_HEX" "$OFFSET_HEX" "$ROMFS" <<'PY'
import os, struct, sys

app_addr = int(sys.argv[1], 16)
offset   = int(sys.argv[2], 16)
romfs_p  = sys.argv[3]
mount    = app_addr + offset
size     = os.path.getsize(romfs_p)

def die(msg):
    sys.stderr.write("[romfs] 错误: %s\n" % msg)
    sys.exit(1)

# 1) the target address must be 4K aligned, otherwise erasing reaches into app's sector
if offset % 4096 != 0:
    die("ROMFS_OFFSET=%#x 未按 4KB 对齐 —— 烧它会擦掉 app 尾部。\n"
        "       重新打包: ./tools/scripts/build.sh --bank <N> --pack" % offset)
if mount % 4096 != 0:
    die("挂载地址 %#x 未按 4KB 对齐" % mount)

# 2) the directory entry in the reloc file must really point at this mount address, otherwise it is a stale image
with open(romfs_p, "rb") as f:
    head = f.read(4 * 4 + 64)
if len(head) < 16:
    die("romfs 文件太小")
typ, name_p, data_p, nchild = struct.unpack_from("<IIII", head, 0)
if typ != 1:
    die("%s 不是 romfs 目录（首项 type=%d）" % (romfs_p, typ))
if name_p != mount + 16:
    die("%s 的目录指针指向 %#x，与本次挂载地址 %#x 不符。\n"
        "       说明它是按别的偏移打出来的，重新执行: ./tools/scripts/build.sh --bank <N> --pack"
        % (romfs_p, name_p, mount))

print("[romfs] 镜像   = %s" % romfs_p)
print("[romfs] 大小   = %d B (%.2f MB)" % (size, size / 1048576))
print("[romfs] app基址= %#010x" % app_addr)
print("[romfs] 偏移   = %#010x (4KB 对齐 ✓)" % offset)
print("[romfs] 落点   = %#010x .. %#010x" % (mount, mount + size))
print("[romfs] 擦除区 = %#010x .. %#010x (%d 个 4KB 扇区)"
      % (mount // 4096 * 4096, (mount + size + 4095) // 4096 * 4096,
         ((mount + size + 4095) // 4096 * 4096 - mount // 4096 * 4096) // 4096))
print("[romfs] 目录项 = %d 个，首项指针 %#x ✓" % (nchild, name_p))
print("%#x" % mount)
PY
) || exit 1

# the last line is the mount address
ADDR=$(echo "$MOUNT_HEX" | tail -1)
echo "$MOUNT_HEX" | sed '$d'

# ---- WSLInterop detection (same as scripts/flash-wsl-app.sh) ----
interop_ok=0
if [ -r /proc/sys/fs/binfmt_misc/WSLInterop ]; then
    grep -q '^enabled' /proc/sys/fs/binfmt_misc/WSLInterop && interop_ok=1
fi

FW_WIN="$(wslpath -w "$ROMFS")"

echo "[romfs] exe  = $MPCLI_EXE"
echo "[romfs] port = $PORT   addr = $ADDR   baud = $BAUD"

if [ "$interop_ok" = 1 ]; then
    cd "$(dirname "$MPCLI_EXE")"
    set -- -c "$PORT" -T RTL87X3G -M 5 -p -A "$ADDR" -F "$FW_WIN" -b "$BAUD" -r -u -d
    if [ "$DRY" = "1" ]; then
        printf '[romfs][dry-run] %q ' "$MPCLI_EXE"; printf '%q ' "$@"; echo
        exit 0
    fi
    exec "$MPCLI_EXE" "$@"
fi

# ---- fallback: WSLInterop not registered, use /init + temp .bat ----
echo "[romfs] 提示: WSLInterop 未注册，改用 /init + 临时 .bat" >&2
[ -x /init ] || die "找不到 /init；请在 Windows 端用 mpcli.exe 手动烧录"

BAT_DIR=""
for d in /mnt/c/Windows/Temp /mnt/c/Users/*/AppData/Local/Temp; do
    [ -d "$d" ] && [ -w "$d" ] && { BAT_DIR="$d"; break; }
done
[ -n "$BAT_DIR" ] || die "找不到可写的 Windows 临时目录"

TMP_BAT="$(mktemp "$BAT_DIR/mpcli-romfs-XXXXXX.bat")"
trap 'rm -f "$TMP_BAT"' EXIT INT TERM
printf '@echo off\r\n"%s" -c %s -T RTL87X3G -M 5 -p -A %s -F "%s" -b %s -r -u -d\r\nexit /b %%ERRORLEVEL%%\r\n' \
    "$(wslpath -w "$MPCLI_EXE")" "$PORT" "$ADDR" "$FW_WIN" "$BAUD" > "$TMP_BAT"

if [ "$DRY" = "1" ]; then
    echo "[romfs][dry-run] /init cmd.exe /c \"$(wslpath -w "$TMP_BAT")\""
    tr -d '\r' < "$TMP_BAT" | sed 's/^/    /'
    exit 0
fi

cd "$BAT_DIR"
/init /mnt/c/Windows/System32/cmd.exe /c "$(wslpath -w "$TMP_BAT")"
rc=$?
rm -f "$TMP_BAT"
trap - EXIT INT TERM
exit $rc
