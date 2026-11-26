#!/usr/bin/env bash
# Flash an arbitrary file to an explicit address with Windows mpcli.exe from WSL2.
# The caller must provide both the file and the 4 KB-aligned flash address.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SECTOR=4096

FW="${1:-}"
ADDR="${2:-}"
PORT="${3:-${PORT:-COM13}}"
MPCLI_EXE="${MPCLI_EXE:-$SCRIPT_DIR/tool/mpcli/mpcli.exe}"
BAUD="${BAUD:-2000000}"

die() { echo "[flash-wsl-direct] $*" >&2; exit 1; }

[ -n "$FW" ] || die "usage: $(basename "$0") <file> <address> [port]"
[ -n "$ADDR" ] || die "an explicit flash address is required"
[[ "$ADDR" =~ ^0[xX][0-9a-fA-F]+$ ]] || die "bad address '$ADDR' (expected 0x-prefixed hex)"
[ $(( $((ADDR)) % SECTOR )) -eq 0 ] || die "address $ADDR is not 4KB aligned"
[ -f "$FW" ] || die "file not found: $FW"
[ -f "$MPCLI_EXE" ] || die "mpcli.exe not found: $MPCLI_EXE"
command -v wslpath >/dev/null 2>&1 || die "wslpath is required"

FW_WIN="$(wslpath -w "$(realpath "$FW")")"
FW_SIZE="$(stat -c%s "$FW")"

echo "[flash-wsl-direct] exe  = $MPCLI_EXE"
echo "[flash-wsl-direct] port = $PORT   addr = $ADDR   baud = $BAUD"
echo "[flash-wsl-direct] fw   = $FW ($FW_SIZE bytes)"
echo "[flash-wsl-direct] fw(win) = $FW_WIN"

cd "$(dirname "$MPCLI_EXE")"
set -- -c "$PORT" -T RTL87X3G -M 5 -p -A "$ADDR" -F "$FW_WIN" -b "$BAUD" -r -u -d

if [ "${DRY:-0}" = "1" ]; then
  printf '[flash-wsl-direct][dry-run] %q ' "$MPCLI_EXE"; printf '%q ' "$@"; echo
  exit 0
fi

exec "$MPCLI_EXE" "$@"
