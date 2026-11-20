#!/usr/bin/env bash
# 只烧 romfs（壁纸/UI 资源），不动 app —— 改素材后的快速验证通道。
#
# 为什么能只烧这一段：build.sh --pack 会把 romfs 放到 4KB 对齐的地址，
# 使它与 app 不共用任何 flash 擦除扇区。所以烧 romfs 时擦除的是
# [romfs 起点向下取整到 4KB, romfs 末尾向上取整]，app 代码完全不受影响。
#
# 前提：
#   1. 已经跑过 ./tools/scripts/build.sh --bank <N> --pack
#      （生成 bin/bank<N>/app_romfs_bank<N>_reloc.bin）
#   2. 那份 reloc 文件里的目录项指针已经指向本次的挂载地址
#      —— 直接用 app/designer/build/app_romfs_*.bin 是不行的，它的指针还是
#         旧的绝对地址，烧上去 UI 会全黑。
#
# WSL 下必须用 Windows 版 mpcli.exe：COM 口是 Windows 设备，Linux 版
# mpcli 在 WSL 里打不开。这里沿用 scripts/flash-wsl-app.sh 的两条路径：
#   1) WSLInterop 已注册 -> 直接执行 .exe
#   2) 未注册           -> /init + 临时 .bat
#
# 用法：
#   ./tools/scripts/flash_romfs.sh                    # 默认 bank0 + COM5
#   ./tools/scripts/flash_romfs.sh --bank 1
#   ./tools/scripts/flash_romfs.sh --port COM7
#   DRY=1 ./tools/scripts/flash_romfs.sh              # 只打印命令
#
# 镜像地址由以下两处推出，不在此脚本硬编码：
#   APP 基址   ← flash_map.ini 的 BANK<N>_APP_ADDR
#   ROMFS 偏移 ← app/designer/src[/user]/romfs_layout.h 的 ROMFS_OFFSET
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"                 # 应用工程根目录 = 仓库根

MPCLI_EXE="${MPCLI_EXE:-$APP_DIR/scripts/tool/mpcli/mpcli.exe}"
FLASH_MAP="$APP_DIR/flash_map.ini"

# romfs_layout.h 可能在 src/ 或 src/user/（Designer 新导出布局）。
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
        *)  PORT="$1"; shift ;;          # 位置参数 = 端口
    esac
done

case "$BANK" in
    0|1) ;;
    *) echo "错误: --bank 只能是 0 或 1" >&2; exit 1 ;;
esac

ROMFS="$APP_DIR/bin/bank${BANK}/app_romfs_bank${BANK}_reloc.bin"

die() { echo "[romfs] 错误: $*" >&2; exit 1; }

# ---- 前置检查 ----
[ -f "$MPCLI_EXE" ] || die "找不到 mpcli.exe: $MPCLI_EXE"
[ -f "$ROMFS" ] || die "找不到 $ROMFS
       先执行: ./tools/scripts/build.sh --bank $BANK --pack"
[ -f "$FLASH_MAP" ] || die "找不到 flash_map.ini"
[ -f "$LAYOUT_H" ] || die "找不到 romfs_layout.h"

# 偏移：ROMFS_OFFSET 0x000F2000
OFFSET_HEX=$(grep -oE '#define[[:space:]]+ROMFS_OFFSET[[:space:]]+(0x[0-9A-Fa-f]+)' "$LAYOUT_H" \
             | grep -oE '0x[0-9A-Fa-f]+' | head -1)
[ -n "$OFFSET_HEX" ] || die "romfs_layout.h 里没有 ROMFS_OFFSET"

# app 基址：BANK0_APP_ADDR=0x7009E000
APP_ADDR_HEX=$(grep -E "^BANK${BANK}_APP_ADDR=" "$FLASH_MAP" | cut -d= -f2 | tr -d '[:space:]')
[ -n "$APP_ADDR_HEX" ] || die "flash_map.ini 里没有 BANK${BANK}_APP_ADDR"

# ---- 校验镜像（4K 对齐 / 指针 / 目录结构）----
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

# 1) 落点必须 4K 对齐：否则擦除会波及 app 所在扇区
if offset % 4096 != 0:
    die("ROMFS_OFFSET=%#x 未按 4KB 对齐 —— 烧它会擦掉 app 尾部。\n"
        "       重新打包: ./tools/scripts/build.sh --bank <N> --pack" % offset)
if mount % 4096 != 0:
    die("挂载地址 %#x 未按 4KB 对齐" % mount)

# 2) reloc 文件的目录项必须真的指向本次挂载地址，否则是旧包
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

# 最后一行是挂载地址
ADDR=$(echo "$MOUNT_HEX" | tail -1)
echo "$MOUNT_HEX" | sed '$d'

# ---- WSLInterop 检测（同 scripts/flash-wsl-app.sh）----
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

# ---- 回退：WSLInterop 未注册，走 /init + 临时 .bat ----
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
