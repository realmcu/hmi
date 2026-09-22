#!/usr/bin/env bash
#
# flash-wsl-romfs.sh —— 在 WSL2 里驱动 Windows 版 mpcli.exe 烧录 HoneyGUI romfs
# 资源镜像到 USER_DATA 区。
#
# 与 flash-wsl-app.sh 的区别：那个烧 App（bin/app.bin → 0x7009E000，写死），
# 本脚本烧资源镜像，镜像和地址由调用方给。
#
# 地址取 flash_map.h 的分区地址（USER_DATA1_ADDR = 0x704D1000），必须与
# ui/project.json 的 romfsBaseAddr、Entry.c 的 gui_vfs_mount_romfs 一致 ——
# romfs 指针按基址烘焙成绝对地址，烧错地址不报错，只在运行时挂载失败。
# 地址还须 4KB 扇区对齐，否则 mpcli 擦除起点上调但长度不减，头部若干字节
# 未擦除导致 flash verify 失败（status 52）。两点都会检查。
#
# 镜像是 mkromfs 的裸产物，不带 MP header，直接烧。
#
# 用法：
#   scripts/flash-wsl-romfs.sh <bin> <addr> [port]
#
#   scripts/flash-wsl-romfs.sh ../ui/build/app_romfs_0x704D1000.bin 0x704D1000
#   scripts/flash-wsl-romfs.sh ../ui/build/app_romfs_0x704D1000.bin 0x704D1000 COM4
#   PORT=COM8 BAUD=3000000 scripts/flash-wsl-romfs.sh <bin> 0x704D1000
#   DRY=1 scripts/flash-wsl-romfs.sh <bin> 0x704D1000   # 只打印命令，不真正烧录
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SECTOR=4096

# ---- 可通过位置参数或环境变量覆盖；FW / ADDR 必填，无默认值 ----
FW="${1:-${FW:-}}"
ADDR="${2:-${ADDR:-}}"
PORT="${3:-${PORT:-COM13}}"
MPCLI_EXE="${MPCLI_EXE:-$SCRIPT_DIR/tool/mpcli/mpcli.exe}"
BAUD="${BAUD:-2000000}"

# ---- 检查 ----
if [ -z "$FW" ] || [ -z "$ADDR" ]; then
  me="$(basename "${BASH_SOURCE[0]}")"
  echo "[flash-romfs] 用法: $me <bin> <addr> [port]" >&2
  echo "[flash-romfs]   例: $me ../ui/build/app_romfs_0x704D1000.bin 0x704D1000 COM4" >&2
  exit 1
fi

[[ "$ADDR" =~ ^0[xX][0-9a-fA-F]+$ ]] || {
  echo "[flash-romfs] 地址格式不对: $ADDR（需 0x 开头的十六进制）" >&2; exit 1; }
[ -f "$MPCLI_EXE" ] || { echo "[flash-romfs] mpcli.exe 不存在: $MPCLI_EXE" >&2; exit 1; }
[ -f "$FW" ]        || { echo "[flash-romfs] 镜像不存在: $FW" >&2; exit 1; }
[ $(( $((ADDR)) % SECTOR )) -eq 0 ] || {
  echo "[flash-romfs] 地址 $ADDR 不是 4KB 扇区对齐，烧录会 verify 失败；请用 flash_map.h 的分区地址" >&2
  exit 1; }

# Designer 把基址写进文件名，校验它与 <addr> 一致
FW_BASE="$(basename "$FW" | grep -oiE '0x[0-9a-f]{6,8}' | head -1 || true)"
[ -z "$FW_BASE" ] || [ "$((FW_BASE))" -eq "$((ADDR))" ] || {
  echo "[flash-romfs] 镜像名里的基址 $FW_BASE 与地址 $ADDR 不一致（会挂载失败）" >&2; exit 1; }

FW_WIN="$(wslpath -w "$(realpath "$FW")")"

echo "[flash-romfs] exe  = $MPCLI_EXE"
echo "[flash-romfs] port = $PORT   addr = $ADDR   baud = $BAUD"
echo "[flash-romfs] fw   = $FW ($(stat -c%s "$FW") bytes)"
echo "[flash-romfs] fw(win) = $FW_WIN"

# mpcli.exe(frozen) 用 sys.executable 目录定位 fw/、config/，与 CWD 无关；
# 这里 cd 到 exe 目录纯粹是和 flash-wsl-app.sh 的行为保持一致。
cd "$(dirname "$MPCLI_EXE")"

set -- -c "$PORT" -T RTL87X3G -M 5 -p -A "$ADDR" -F "$FW_WIN" -b "$BAUD" -r -u -d

if [ "${DRY:-0}" = "1" ]; then
  printf '[flash-romfs][dry-run] %q ' "$MPCLI_EXE"; printf '%q ' "$@"; echo
  exit 0
fi

exec "$MPCLI_EXE" "$@"
