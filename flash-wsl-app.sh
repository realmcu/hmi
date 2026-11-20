#!/usr/bin/env bash
#
# flash.sh -- drive the Windows mpcli.exe from WSL2 to flash the RTL87X3G App image.
#
# How it works: WSL directly invokes the Windows mpcli.exe (a native process that can access the COM port),
#       and uses wslpath -w to convert the WSL firmware path into Windows form for -F.
#
# Default configuration matches "West Flash" in .vscode/tasks.json:
#   - Tool: v4.0.0.7 Windows mpcli.exe (scripts/tool/mpcli/mpcli.exe, bundled with the repo)
#   - Firmware: bin/app.bin (no MP header)
#   - Flash: -p -A 0x7009E000 -b 2000000 -M 5 -r -u -d -T RTL87X3G
#
# Usage:
#   scripts/flash-wsl-app.sh                 # default COM13
#   scripts/flash-wsl-app.sh COM8            # specify the download port
#   PORT=COM8 BAUD=2000000 scripts/flash-wsl-app.sh
#   DRY=1 scripts/flash-wsl-app.sh           # only print the command to run, do not flash
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ---- Can be overridden by positional arguments or environment variables ----
PORT="${1:-${PORT:-COM13}}"
MPCLI_EXE="${MPCLI_EXE:-$SCRIPT_DIR/tool/mpcli/mpcli.exe}"
FW="${FW:-$REPO_ROOT/bin/app.bin}"
ADDR="${ADDR:-0x7009E000}"
BAUD="${BAUD:-2000000}"

# ---- Checks ----
[ -f "$MPCLI_EXE" ] || { echo "[flash] mpcli.exe 不存在: $MPCLI_EXE" >&2; exit 1; }
[ -f "$FW" ]        || { echo "[flash] 固件不存在: $FW（先 west build 生成 bin/app.bin）" >&2; exit 1; }

FW_WIN="$(wslpath -w "$FW")"

echo "[flash] exe  = $MPCLI_EXE"
echo "[flash] port = $PORT   addr = $ADDR   baud = $BAUD"
echo "[flash] fw   = $FW"
echo "[flash] fw(win) = $FW_WIN"

# mpcli.exe(frozen) locates fw/ and config/ via the sys.executable directory, independent of CWD;
# cd-ing to the exe directory here just keeps the cwd consistent with tasks.json.
cd "$(dirname "$MPCLI_EXE")"

set -- -c "$PORT" -T RTL87X3G -M 5 -p -A "$ADDR" -F "$FW_WIN" -b "$BAUD" -r -u -d

if [ "${DRY:-0}" = "1" ]; then
  printf '[flash][dry-run] %q ' "$MPCLI_EXE"; printf '%q ' "$@"; echo
  exit 0
fi

exec "$MPCLI_EXE" "$@"
