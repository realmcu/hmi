#!/usr/bin/env bash
#
# flash.sh -- flash the RTL87X3G App image with mpcli on native Linux.
#
# Differences from flash-wsl-app.sh:
#   - Uses the native Linux mpcli (ELF executable), not the Windows mpcli.exe.
#   - The serial port is /dev/ttyUSB* instead of COMx, and paths need no wslpath conversion.
#
# Default configuration matches the flash parameters of flash-wsl-app.sh:
#   - Tool: mpcli v4.0.0.7 (scripts/tool/mpcli/mpcli, bundled with the repo)
#   - Firmware: bin/app.bin (no MP header)
#   - Flash: -p -A 0x7009E000 -b 2000000 -M 5 -r -u -d -T RTL87X3G
#
# Usage:
#   scripts/flash-linux-app.sh                        # default /dev/ttyUSB0
#   scripts/flash-linux-app.sh /dev/ttyUSB1           # specify the download port
#   PORT=/dev/ttyUSB1 BAUD=3000000 scripts/flash-linux-app.sh
#   DRY=1 scripts/flash-linux-app.sh                  # only print the command to run, do not flash
#
# Note: accessing /dev/ttyUSB* requires the current user to be in the dialout group:
#   sudo usermod -aG dialout "$USER"   # re-login is required after adding the group
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ---- Can be overridden by positional arguments or environment variables ----
PORT="${1:-${PORT:-/dev/ttyUSB0}}"
MPCLI="${MPCLI:-$SCRIPT_DIR/tool/mpcli/mpcli}"
FW="${FW:-$REPO_ROOT/bin/app.bin}"
ADDR="${ADDR:-0x7009E000}"
BAUD="${BAUD:-2000000}"

# ---- Checks ----
[ -x "$MPCLI" ] || { echo "[flash] mpcli 不存在或不可执行: $MPCLI" >&2; exit 1; }
[ -f "$FW" ]    || { echo "[flash] 固件不存在: $FW（先 west build 生成 bin/app.bin）" >&2; exit 1; }
[ -e "$PORT" ]  || { echo "[flash] 串口不存在: $PORT（检查 USB 连接，或 ls /dev/ttyUSB*）" >&2; exit 1; }
[ -w "$PORT" ]  || { echo "[flash] 串口不可写: $PORT（把用户加入 dialout 组：sudo usermod -aG dialout \$USER，重新登录后生效）" >&2; exit 1; }

echo "[flash] mpcli = $MPCLI"
echo "[flash] port  = $PORT   addr = $ADDR   baud = $BAUD"
echo "[flash] fw    = $FW"

# mpcli (frozen) locates fw/ and config/ via the sys.executable directory, independent of CWD;
# cd-ing to the mpcli directory here just keeps behavior consistent with flash-wsl-app.sh.
cd "$(dirname "$MPCLI")"

set -- -c "$PORT" -T RTL87X3G -M 5 -p -A "$ADDR" -F "$FW" -b "$BAUD" -r -u -d

if [ "${DRY:-0}" = "1" ]; then
  printf '[flash][dry-run] %q ' "$MPCLI"; printf '%q ' "$@"; echo
  exit 0
fi

exec "$MPCLI" "$@"
