#!/usr/bin/env bash
#
# flash-runtime-wsl.sh -- drive the Windows mpcli.exe from WSL2 to flash the bundled runtime firmware.
#
# Differences from flash-wsl-app.sh:
#   - flash-wsl-app.sh flashes a single App image (-p -A 0x7009E000 -F bin/app.bin).
#   - This script flashes the whole runtime (boot patch / upperstack / OTA header /
#     stack & sys patch / DSP sys+app+cfg / SYSTEM & APP Config, 11 images in total),
#     using mpcli's --flash-runtime to generate flash_image.json in one command, then -a flash all.
#
# The runtime does **not include the APP** (0x7009E000 is not in the address table); full flashing order:
#   1. scripts/flash-runtime-wsl.sh COM13   # flash the runtime first
#   2. scripts/flash-wsl-app.sh COM13        # then flash bin/app.bin
#
# Usage:
#   scripts/flash-runtime-wsl.sh                  # default COM13
#   scripts/flash-runtime-wsl.sh COM8             # specify the COM port
#   PORT=COM8 BAUD=3000000 scripts/flash-runtime-wsl.sh
#   CHIP=RTL8783G scripts/flash-runtime-wsl.sh    # switch chip
#   DRY=1 scripts/flash-runtime-wsl.sh COM8       # only print the command to run
#
# Note: the firmware directory needs no wslpath conversion -- mpcli.exe locates fw/ itself via sys.executable,
#       resolving it to a \\wsl.localhost\... UNC path. The cost is that UNC is slower than native Linux
#       (scanning 11 bins takes ~2.5s vs 0.03s); if that is too slow, copy fw/runtime_system_bin/<CHIP>
#       to a Windows drive and use the two-step flash via mpcli.exe --gen-json --bin-dir <that directory>.
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ---- Can be overridden by positional arguments or environment variables ----
PORT="${1:-${PORT:-COM13}}"
MPCLI_EXE="${MPCLI_EXE:-$SCRIPT_DIR/tool/mpcli/mpcli.exe}"
# CHIP is the directory name under tool/mpcli/fw/runtime_system_bin/: RTL8773G / RTL8783G / RTL87X3EP
CHIP="${CHIP:-RTL8773G}"
BAUD="${BAUD:-2000000}"

# ---- Checks ----
[ -f "$MPCLI_EXE" ] || { echo "[flash-runtime] mpcli.exe 不存在: $MPCLI_EXE" >&2; exit 1; }
[ -d "$(dirname "$MPCLI_EXE")/fw/runtime_system_bin/$CHIP" ] || {
  echo "[flash-runtime] 芯片目录不存在: fw/runtime_system_bin/$CHIP" >&2
  echo "[flash-runtime] 可选: $(ls "$(dirname "$MPCLI_EXE")/fw/runtime_system_bin" | tr '\n' ' ')" >&2
  exit 1
}

echo "[flash-runtime] exe  = $MPCLI_EXE"
echo "[flash-runtime] port = $PORT   chip = $CHIP   baud = $BAUD"

# mpcli.exe(frozen) locates fw/ and config/ via the sys.executable directory, independent of CWD;
# cd-ing to the exe directory here just keeps behavior consistent with flash-wsl-app.sh.
cd "$(dirname "$MPCLI_EXE")"

# -u -d must be given explicitly: mpcli's --flash-runtime only injects -M 5 -r -b 3000000,
# but SYSTEM_Config lands in the protected 0x70002000 (OEM_CFG region), and without -u it cannot be written.
# -T is derived automatically from CHIP (RTL8773G -> RTL87X3G), no need to pass it.
set -- --flash-runtime "$CHIP" -c "$PORT" -u -d -b "$BAUD"

if [ "${DRY:-0}" = "1" ]; then
  printf '[flash-runtime][dry-run] %q ' "$MPCLI_EXE"; printf '%q ' "$@"; echo
  exit 0
fi

exec "$MPCLI_EXE" "$@"
