#!/usr/bin/env bash
#
# flash.sh -- drive the Windows mpcli.exe from WSL2 to flash the RTL87X3G App image.
#
# How it works: WSL invokes the Windows mpcli.exe directly (a native process that can access the COM port),
#       and uses wslpath -w to convert the firmware path from WSL to Windows form for -F.
#
# Default configuration mirrors "West Flash" in .vscode/tasks.json:
#   - Tool: v4.0.0.6 Windows mpcli.exe
#   - Firmware: bin/app.bin (no MP header)
#   - Flash: -p -A 0x7009E000 -b 3000000 -M 5 -r -u -d -T RTL87X3G
#
# Usage:
#   scripts/flash.sh                 # default COM7
#   scripts/flash.sh COM8            # specify the download port
#   PORT=COM8 BAUD=2000000 scripts/flash.sh
#   DRY=1 scripts/flash.sh           # only print the commands to run, do not actually flash
#
# Before flashing, make sure the device is in MP download mode (tie P2_0 to GND then reset).
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# ---- Can be overridden via positional arguments or environment variables ----
PORT="${1:-${PORT:-COM7}}"
MPCLI_EXE="${MPCLI_EXE:-/mnt/d/mpcli_meta_tool_v4.0.0.6_win/mpcli.exe}"
FW="${FW:-$REPO_ROOT/bin/app.bin}"
ADDR="${ADDR:-0x7009E000}"
BAUD="${BAUD:-3000000}"

# ---- Checks ----
[ -f "$MPCLI_EXE" ] || { echo "[flash] mpcli.exe 不存在: $MPCLI_EXE" >&2; exit 1; }
[ -f "$FW" ]        || { echo "[flash] 固件不存在: $FW（先 west build 生成 bin/app.bin）" >&2; exit 1; }

FW_WIN="$(wslpath -w "$FW")"

echo "[flash] exe  = $MPCLI_EXE"
echo "[flash] port = $PORT   addr = $ADDR   baud = $BAUD"
echo "[flash] fw   = $FW"
echo "[flash] fw(win) = $FW_WIN"

# mpcli.exe (frozen) locates fw/ and config/ via the sys.executable directory, independent of CWD;
# cd'ing to the exe directory here is purely to match the cwd in tasks.json.
cd "$(dirname "$MPCLI_EXE")"

set -- -c "$PORT" -T RTL87X3G -M 5 -p -A "$ADDR" -F "$FW_WIN" -b "$BAUD" -r -u -d

if [ "${DRY:-0}" = "1" ]; then
  printf '[flash][dry-run] %q ' "$MPCLI_EXE"; printf '%q ' "$@"; echo
  exit 0
fi

exec "$MPCLI_EXE" "$@"
