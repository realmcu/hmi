#!/usr/bin/env bash
# Flash an arbitrary file to an explicit address on a Windows workstation
# reached through an SSH alias.
#
# This is the low-level address-based primitive. It does not parse MP headers,
# IMG headers or image IDs, and it does not choose an address for the caller.
# Both this script and flash-linux2win-app.sh share the same provisioned
# %USERPROFILE%\.rtl87-flash environment, so running one after the other does
# not re-provision.
#
# The address must be 4 KB aligned, otherwise mpcli rounds the erase start up
# while keeping the full length and verification can fail.
#
# Usage:
#   scripts/flash-linux2win-direct-addr.sh <file> <address> [port]
#   DRY=1 scripts/flash-linux2win-direct-addr.sh <file> <address>

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SECTOR=4096

WIN_HOST="${WIN_HOST:-main-win-pc}"
PORT="${3:-${PORT:-COM4}}"
FW="${1:-}"
ADDR="${2:-}"
BAUD="${BAUD:-2000000}"
DRY="${DRY:-0}"

die() { echo "[ssh-direct] $*" >&2; exit 1; }

[ -n "$FW" ] || die "usage: $0 <file> <address> [port]"
[ -n "$ADDR" ] || die "an explicit flash address is required"
[ -f "$FW" ] || die "file not found: $FW"

[[ "$ADDR" =~ ^0[xX][0-9a-fA-F]+$ ]] || die "bad address '$ADDR' (expected 0x-prefixed hex)"
[ $(( $((ADDR)) % SECTOR )) -eq 0 ] || {
  die "address $ADDR is not 4KB sector aligned"; }

FW_SIZE="$(stat -c%s "$FW")"

if [ "$DRY" = "1" ]; then
  echo "[ssh-direct][dry-run] host = $WIN_HOST   port = $PORT"
  echo "[ssh-direct][dry-run] fw   = $FW ($FW_SIZE bytes)"
  echo "[ssh-direct][dry-run] addr = $ADDR   baud = $BAUD"
  echo "[ssh-direct][dry-run] env  = %USERPROFILE%\\.rtl87-flash"
  exit 0
fi

# ---- preconditions ----------------------------------------------------------
for t in ssh scp tar sha256sum; do
  command -v "$t" >/dev/null 2>&1 || die "$t not found"
done

WIN_PROFILE="$(ssh -o BatchMode=yes "$WIN_HOST" 'echo %USERPROFILE%' | tr -d '\r' | head -n 1)"
WIN_TEMP="$(ssh -o BatchMode=yes "$WIN_HOST" 'echo %TEMP%' | tr -d '\r' | head -n 1)"

[[ "$WIN_PROFILE" =~ ^[A-Za-z]:\\ ]] || die "cannot resolve Windows %USERPROFILE% through $WIN_HOST: $WIN_PROFILE"
[[ "$WIN_TEMP" =~ ^[A-Za-z]:\\ ]] || die "cannot resolve Windows %TEMP% through $WIN_HOST: $WIN_TEMP"

ENV_DIR="${WIN_PROFILE}\\.rtl87-flash"
WIN_FLASH="${ENV_DIR}\\flash-win32-app.bat"
WIN_RUNTIME_FLASH="${ENV_DIR}\\flash-runtime-win32.bat"
WIN_MPCLI="${ENV_DIR}\\tool\\mpcli\\mpcli.exe"
WIN_MARKER="${ENV_DIR}\\.bundle.sha256"

# Keep this signature identical to the other flash-linux2win-* scripts. The
# shared environment contains both app and runtime launchers plus one mpcli tree.
SIGNATURE="$(
  cd "$SCRIPT_DIR"
  {
    sha256sum flash-win32-app.bat flash-runtime-win32.bat
    find tool/mpcli -type f -print0 | sort -z | xargs -0 sha256sum
  } | sha256sum | awk '{print $1}'
)"

REMOTE_STATE="$(ssh -o BatchMode=yes "$WIN_HOST" "if exist \"$WIN_FLASH\" (if exist \"$WIN_RUNTIME_FLASH\" (if exist \"$WIN_MPCLI\" (if exist \"$WIN_MARKER\" (type \"$WIN_MARKER\") else (echo MISSING)) else (echo MISSING)) else (echo MISSING)) else (echo MISSING)" | tr -d '\r' | head -n 1)"

if [ "$REMOTE_STATE" != "$SIGNATURE" ]; then
  echo "[ssh-direct] provisioning Windows environment"
  WORK_DIR="$(mktemp -d)"
  trap 'rm -rf "$WORK_DIR"' EXIT
  STAGE_DIR="$WORK_DIR/bundle"
  mkdir -p "$STAGE_DIR/tool"
  cp "$SCRIPT_DIR/flash-win32-app.bat" "$STAGE_DIR/"
  cp "$SCRIPT_DIR/flash-runtime-win32.bat" "$STAGE_DIR/"
  cp -a "$SCRIPT_DIR/tool/mpcli" "$STAGE_DIR/tool/mpcli"
  BUNDLE="$WORK_DIR/rtl87-flash-env.tgz"
  tar -czf "$BUNDLE" -C "$STAGE_DIR" .

  REMOTE_BUNDLE_NAME="rtl87_env_$(date +%Y%m%d_%H%M%S)_$$.tgz"
  REMOTE_BUNDLE="${WIN_TEMP}\\${REMOTE_BUNDLE_NAME}"
  REMOTE_BUNDLE_SCP="${WIN_TEMP//\\//}/${REMOTE_BUNDLE_NAME}"

  scp -O -q "$BUNDLE" "${WIN_HOST}:${REMOTE_BUNDLE_SCP}"
  ssh -o BatchMode=yes "$WIN_HOST" "if not exist \"$ENV_DIR\" mkdir \"$ENV_DIR\""
  ssh -o BatchMode=yes "$WIN_HOST" "tar -xzf \"$REMOTE_BUNDLE\" -C \"$ENV_DIR\""
  ssh -o BatchMode=yes "$WIN_HOST" "echo $SIGNATURE>\"$WIN_MARKER\""
  ssh -o BatchMode=yes "$WIN_HOST" "del /q \"$REMOTE_BUNDLE\" >nul 2>nul" || true
  echo "[ssh-direct] Windows environment ready"
else
  echo "[ssh-direct] Windows environment cached"
fi

REMOTE_NAME="rtl87_direct_$(date +%Y%m%d_%H%M%S)_$$.bin"
REMOTE_WIN_FW="${WIN_TEMP}\\${REMOTE_NAME}"
REMOTE_SCP_FW="${WIN_TEMP//\\//}/${REMOTE_NAME}"

echo "[ssh-direct] host = $WIN_HOST   port = $PORT"
echo "[ssh-direct] fw   = $FW ($FW_SIZE bytes)"
echo "[ssh-direct] addr = $ADDR   baud = $BAUD"
echo "[ssh-direct] upload = $REMOTE_WIN_FW"

scp -O -q "$FW" "${WIN_HOST}:${REMOTE_SCP_FW}"

remote_cmd="set \"FW=${REMOTE_WIN_FW}\" && set \"ADDR=${ADDR}\" && set \"BAUD=${BAUD}\" && set \"NOPAUSE=1\" && call \"${WIN_FLASH}\" ${PORT}"
set +e
ssh "$WIN_HOST" "$remote_cmd"
RC=$?
set -e

if [ "$RC" -eq 0 ]; then
  ssh -o BatchMode=yes "$WIN_HOST" "del /q \"${REMOTE_WIN_FW}\" >nul 2>nul" || true
  echo "[ssh-direct] done; wrote $FW_SIZE bytes at $ADDR"
else
  echo "[ssh-direct] flash failed; remote file kept for diagnostics: $REMOTE_WIN_FW" >&2
fi
exit "$RC"
