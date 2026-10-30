#!/usr/bin/env bash
# Flash an app image on a Windows workstation reached through an SSH alias.
#
# The first run provisions %USERPROFILE%\.rtl87-flash from the Linux checkout.
# Later runs reuse that environment and upload only the app image.
#
# Usage:
#   scripts/flash-ssh-app.sh
#   scripts/flash-ssh-app.sh COM4
#   WIN_HOST=main-win-pc FW=/path/to/app.bin scripts/flash-ssh-app.sh
#   DRY=1 scripts/flash-ssh-app.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

WIN_HOST="${WIN_HOST:-main-win-pc}"
PORT="${1:-${PORT:-COM4}}"
FW="${FW:-$REPO_ROOT/bin/app.bin}"
ADDR="${ADDR:-0x7009E000}"
BAUD="${BAUD:-2000000}"
DRY="${DRY:-0}"

[ -f "$FW" ] || { echo "[ssh-flash] firmware not found: $FW (run scripts/build.sh first)" >&2; exit 1; }

if [ "$DRY" = "1" ]; then
  echo "[ssh-flash][dry-run] host = $WIN_HOST   port = $PORT"
  echo "[ssh-flash][dry-run] fw   = $FW"
  echo "[ssh-flash][dry-run] addr = $ADDR   baud = $BAUD"
  echo "[ssh-flash][dry-run] env  = %USERPROFILE%\\.rtl87-flash"
  echo "[ssh-flash][dry-run] provision from tool/mpcli and flash-win32-app.bat when missing"
  exit 0
fi

command -v ssh >/dev/null 2>&1 || { echo "[ssh-flash] ssh not found" >&2; exit 1; }
command -v scp >/dev/null 2>&1 || { echo "[ssh-flash] scp not found" >&2; exit 1; }
command -v tar >/dev/null 2>&1 || { echo "[ssh-flash] tar not found" >&2; exit 1; }
command -v sha256sum >/dev/null 2>&1 || { echo "[ssh-flash] sha256sum not found" >&2; exit 1; }

WIN_PROFILE="$(ssh -o BatchMode=yes "$WIN_HOST" 'echo %USERPROFILE%' | tr -d '\r' | head -n 1)"
WIN_TEMP="$(ssh -o BatchMode=yes "$WIN_HOST" 'echo %TEMP%' | tr -d '\r' | head -n 1)"

[[ "$WIN_PROFILE" =~ ^[A-Za-z]:\\ ]] || {
  echo "[ssh-flash] cannot resolve Windows %USERPROFILE% through $WIN_HOST: $WIN_PROFILE" >&2
  exit 1
}
[[ "$WIN_TEMP" =~ ^[A-Za-z]:\\ ]] || {
  echo "[ssh-flash] cannot resolve Windows %TEMP% through $WIN_HOST: $WIN_TEMP" >&2
  exit 1
}

ENV_DIR="${WIN_PROFILE}\\.rtl87-flash"
WIN_FLASH="${ENV_DIR}\\flash-win32-app.bat"
WIN_MPCLI="${ENV_DIR}\\tool\\mpcli\\mpcli.exe"
WIN_MARKER="${ENV_DIR}\\.bundle.sha256"

SIGNATURE="$(
  cd "$SCRIPT_DIR"
  {
    sha256sum flash-win32-app.bat
    find tool/mpcli -type f -print0 | sort -z | xargs -0 sha256sum
  } | sha256sum | awk '{print $1}'
)"

REMOTE_STATE="$(ssh -o BatchMode=yes "$WIN_HOST" "if exist \"$WIN_FLASH\" (if exist \"$WIN_MPCLI\" (if exist \"$WIN_MARKER\" (type \"$WIN_MARKER\") else (echo MISSING)) else (echo MISSING)) else (echo MISSING)" | tr -d '\r' | head -n 1)"

if [ "$REMOTE_STATE" != "$SIGNATURE" ]; then
  echo "[ssh-flash] provisioning Windows environment"
  WORK_DIR="$(mktemp -d)"
  trap 'rm -rf "$WORK_DIR"' EXIT
  STAGE_DIR="$WORK_DIR/bundle"
  mkdir -p "$STAGE_DIR/tool"
  cp "$SCRIPT_DIR/flash-win32-app.bat" "$STAGE_DIR/"
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
  echo "[ssh-flash] Windows environment ready"
else
  echo "[ssh-flash] Windows environment cached"
fi

REMOTE_NAME="rtl87_ssh_$(date +%Y%m%d_%H%M%S)_$$.bin"
REMOTE_WIN_FW="${WIN_TEMP}\\${REMOTE_NAME}"
REMOTE_SCP_FW="${WIN_TEMP//\\//}/${REMOTE_NAME}"

echo "[ssh-flash] host = $WIN_HOST   port = $PORT"
echo "[ssh-flash] fw   = $FW"
echo "[ssh-flash] addr = $ADDR   baud = $BAUD"
echo "[ssh-flash] upload = $REMOTE_WIN_FW"

scp -O -q "$FW" "${WIN_HOST}:${REMOTE_SCP_FW}"

remote_cmd="set \"FW=${REMOTE_WIN_FW}\" && set \"ADDR=${ADDR}\" && set \"BAUD=${BAUD}\" && set \"NOPAUSE=1\" && call \"${WIN_FLASH}\" ${PORT}"
set +e
ssh "$WIN_HOST" "$remote_cmd"
RC=$?
set -e

if [ "$RC" -eq 0 ]; then
  ssh -o BatchMode=yes "$WIN_HOST" "del /q \"${REMOTE_WIN_FW}\" >nul 2>nul" || true
else
  echo "[ssh-flash] flash failed; remote image kept for diagnostics: $REMOTE_WIN_FW" >&2
fi
exit "$RC"
