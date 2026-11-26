#!/usr/bin/env bash
#
# flash-runtime-linux.sh -- flash the bundled runtime firmware with mpcli on native Linux.
#
# Differences from flash-linux-app.sh:
#   - flash-linux-app.sh flashes a single App image (-p -A 0x7009E000 -F applications/bin/app.bin).
#   - This script flashes the whole runtime (boot patch / upperstack / OTA header /
#     stack & sys patch / DSP sys+app+cfg / SYSTEM & APP Config, 11 images in total),
#     using mpcli's --flash-runtime to generate flash_image.json in one command, then -a flash all.
#
# The runtime does **not include the APP** (0x7009E000 is not in the address table); full flashing order:
#   1. scripts/flash-runtime-linux.sh      # flash the runtime first
#   2. scripts/flash-linux-app.sh          # then flash applications/bin/app.bin
#
# Usage:
#   scripts/flash-runtime-linux.sh                      # default /dev/ttyUSB0
#   scripts/flash-runtime-linux.sh /dev/ttyUSB1         # specify the download port
#   PORT=/dev/ttyUSB1 BAUD=3000000 scripts/flash-runtime-linux.sh
#   CHIP=RTL8783G scripts/flash-runtime-linux.sh        # switch chip
#   DRY=1 scripts/flash-runtime-linux.sh                # only print the command to run
#
# Note: accessing /dev/ttyUSB* requires the current user to be in the dialout group:
#   sudo usermod -aG dialout "$USER"   # re-login is required after adding the group
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ---- Can be overridden by positional arguments or environment variables ----
PORT="${1:-${PORT:-/dev/ttyUSB0}}"
MPCLI="${MPCLI:-$SCRIPT_DIR/tool/mpcli/mpcli}"
# CHIP is the directory name under tool/mpcli/fw/runtime_system_bin/: RTL8773G / RTL8783G / RTL87X3EP
CHIP="${CHIP:-RTL8773G}"
BAUD="${BAUD:-2000000}"

# ---- Checks ----
[ -x "$MPCLI" ] || { echo "[flash-runtime] mpcli 不存在或不可执行: $MPCLI" >&2; exit 1; }
[ -d "$(dirname "$MPCLI")/fw/runtime_system_bin/$CHIP" ] || {
  echo "[flash-runtime] 芯片目录不存在: fw/runtime_system_bin/$CHIP" >&2
  echo "[flash-runtime] 可选: $(ls "$(dirname "$MPCLI")/fw/runtime_system_bin" | tr '\n' ' ')" >&2
  exit 1
}
[ -e "$PORT" ] || { [ "${DRY:-0}" = "1" ] || { echo "[flash-runtime] 串口不存在: $PORT（检查 USB 连接，或 ls /dev/ttyUSB*）" >&2; exit 1; }; }
[ -w "$PORT" ] || { [ "${DRY:-0}" = "1" ] || { echo "[flash-runtime] 串口不可写: $PORT（把用户加入 dialout 组：sudo usermod -aG dialout \$USER，重新登录后生效）" >&2; exit 1; }; }

echo "[flash-runtime] mpcli = $MPCLI"
echo "[flash-runtime] port  = $PORT   chip = $CHIP   baud = $BAUD"

# mpcli(frozen) locates fw/ and config/ via the sys.executable directory, independent of CWD;
# cd-ing to the mpcli directory here just keeps behavior consistent with flash-linux-app.sh.
cd "$(dirname "$MPCLI")"

# -u -d must be given explicitly: mpcli's --flash-runtime only injects -M 5 -r -b 3000000,
# but SYSTEM_Config lands in the protected 0x70002000 (OEM_CFG region), and without -u it cannot be written.
# -T is derived automatically from CHIP (RTL8773G -> RTL87X3G), no need to pass it.
set -- --flash-runtime "$CHIP" -c "$PORT" -u -d -b "$BAUD"

if [ "${DRY:-0}" = "1" ]; then
  printf '[flash-runtime][dry-run] %q ' "$MPCLI"; printf '%q ' "$@"; echo
  exit 0
fi

exec "$MPCLI" "$@"
