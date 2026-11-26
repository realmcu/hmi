#!/usr/bin/env bash
#
# flash-wsl-romfs.sh -- drive the Windows mpcli.exe from WSL2 to flash the HoneyGUI romfs
# resource image to the USER_DATA area.
#
# Difference from flash-wsl-app.sh: that one flashes the App (applications/bin/app.bin -> 0x7009E000, hard-coded),
# this one flashes a resource image whose image and address are provided by the caller.
#
# The address comes from the flash_map.h partition address (USER_DATA1_ADDR = 0x704D1000), and must match the
# romfsBaseAddr in ui/project.json and gui_vfs_mount_romfs in Entry.c --
# romfs pointers are baked to absolute addresses from the base, so a wrong address does not error, it only fails to mount at runtime.
# The address must also be 4KB sector aligned, otherwise mpcli rounds the erase start up while keeping the full length, leaving the head bytes
# unerased and failing flash verify (status 52). Both points are checked.
#
# The image is the raw mkromfs output, without an MP header, flashed directly.
#
# Usage:
#   scripts/flash-wsl-romfs.sh <bin> <addr> [port]
#
#   scripts/flash-wsl-romfs.sh ui/build/app_romfs_0x704D1000.bin 0x704D1000
#   scripts/flash-wsl-romfs.sh ui/build/app_romfs_0x704D1000.bin 0x704D1000 COM4
#   PORT=COM8 BAUD=3000000 scripts/flash-wsl-romfs.sh <bin> 0x704D1000
#   DRY=1 scripts/flash-wsl-romfs.sh <bin> 0x704D1000   # only print the command, do not flash
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SECTOR=4096

# ---- Can be overridden by positional arguments or environment variables; FW / ADDR are required, no defaults ----
FW="${1:-${FW:-}}"
ADDR="${2:-${ADDR:-}}"
PORT="${3:-${PORT:-COM13}}"
MPCLI_EXE="${MPCLI_EXE:-$SCRIPT_DIR/tool/mpcli/mpcli.exe}"
BAUD="${BAUD:-2000000}"

# ---- Checks ----
if [ -z "$FW" ] || [ -z "$ADDR" ]; then
  me="$(basename "${BASH_SOURCE[0]}")"
  echo "[flash-romfs] 用法: $me <bin> <addr> [port]" >&2
  echo "[flash-romfs]   例: $me ui/build/app_romfs_0x704D1000.bin 0x704D1000 COM4" >&2
  exit 1
fi

[[ "$ADDR" =~ ^0[xX][0-9a-fA-F]+$ ]] || {
  echo "[flash-romfs] 地址格式不对: $ADDR（需 0x 开头的十六进制）" >&2; exit 1; }
[ -f "$MPCLI_EXE" ] || { echo "[flash-romfs] mpcli.exe 不存在: $MPCLI_EXE" >&2; exit 1; }
[ -f "$FW" ]        || { echo "[flash-romfs] 镜像不存在: $FW" >&2; exit 1; }
[ $(( $((ADDR)) % SECTOR )) -eq 0 ] || {
  echo "[flash-romfs] 地址 $ADDR 不是 4KB 扇区对齐，烧录会 verify 失败；请用 flash_map.h 的分区地址" >&2
  exit 1; }

# The Designer writes the base address into the file name; verify it matches <addr>
FW_BASE="$(basename "$FW" | grep -oiE '0x[0-9a-f]{6,8}' | head -1 || true)"
[ -z "$FW_BASE" ] || [ "$((FW_BASE))" -eq "$((ADDR))" ] || {
  echo "[flash-romfs] 镜像名里的基址 $FW_BASE 与地址 $ADDR 不一致（会挂载失败）" >&2; exit 1; }

FW_WIN="$(wslpath -w "$(realpath "$FW")")"

echo "[flash-romfs] exe  = $MPCLI_EXE"
echo "[flash-romfs] port = $PORT   addr = $ADDR   baud = $BAUD"
echo "[flash-romfs] fw   = $FW ($(stat -c%s "$FW") bytes)"
echo "[flash-romfs] fw(win) = $FW_WIN"

# mpcli.exe(frozen) locates fw/ and config/ via the sys.executable directory, independent of CWD;
# cd-ing to the exe directory here just keeps behavior consistent with flash-wsl-app.sh.
cd "$(dirname "$MPCLI_EXE")"

set -- -c "$PORT" -T RTL87X3G -M 5 -p -A "$ADDR" -F "$FW_WIN" -b "$BAUD" -r -u -d

if [ "${DRY:-0}" = "1" ]; then
  printf '[flash-romfs][dry-run] %q ' "$MPCLI_EXE"; printf '%q ' "$@"; echo
  exit 0
fi

exec "$MPCLI_EXE" "$@"
