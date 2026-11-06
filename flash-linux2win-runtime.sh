#!/usr/bin/env bash
# Flash the bundled runtime on a Windows workstation reached through an SSH alias.
#
# The board is connected to $WIN_HOST. This Linux-side script provisions the
# Windows mpcli environment on first use, then runs flash-runtime-win32.bat
# remotely. The runtime images are already part of the provisioned mpcli tree.
#
# The runtime does NOT include the application. Typical full-board sequence:
#   scripts/flash-linux2win-runtime.sh COM4
#   scripts/flash-linux2win-app.sh COM4
#
# Usage:
#   scripts/flash-linux2win-runtime.sh
#   scripts/flash-linux2win-runtime.sh COM8
#   WIN_HOST=main-win-pc CHIP=RTL8783G scripts/flash-linux2win-runtime.sh COM8
#   DRY=1 scripts/flash-linux2win-runtime.sh COM8

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

WIN_HOST="${WIN_HOST:-main-win-pc}"
PORT="${1:-${PORT:-COM4}}"
CHIP="${CHIP:-RTL8773G}"
BAUD="${BAUD:-2000000}"
DRY="${DRY:-0}"

RUNTIME_ROOT="$SCRIPT_DIR/tool/mpcli/fw/runtime_system_bin"
if [ ! -d "$RUNTIME_ROOT/$CHIP" ]; then
  echo "[ssh-runtime] chip dir not found: fw/runtime_system_bin/$CHIP" >&2
  echo "[ssh-runtime] available: $(find "$RUNTIME_ROOT" -mindepth 1 -maxdepth 1 -type d -printf '%f ' | sort)" >&2
  exit 1
fi

if [ "$DRY" = "1" ]; then
  echo "[ssh-runtime][dry-run] host = $WIN_HOST   port = $PORT"
  echo "[ssh-runtime][dry-run] chip = $CHIP   baud = $BAUD"
  echo "[ssh-runtime][dry-run] env  = %USERPROFILE%\\.rtl87-flash"
  echo "[ssh-runtime][dry-run] provision from tool/mpcli and flash-runtime-win32.bat when missing"
  echo "[ssh-runtime][dry-run] runtime image is currently $RUNTIME_ROOT/$CHIP"
  exit 0
fi

for t in ssh scp tar sha256sum; do
  command -v "$t" >/dev/null 2>&1 || { echo "[ssh-runtime] $t not found" >&2; exit 1; }
done

WIN_PROFILE="$(ssh -o BatchMode=yes "$WIN_HOST" 'echo %USERPROFILE%' | tr -d '\r' | head -n 1)"
WIN_TEMP="$(ssh -o BatchMode=yes "$WIN_HOST" 'echo %TEMP%' | tr -d '\r' | head -n 1)"

[[ "$WIN_PROFILE" =~ ^[A-Za-z]:\\ ]] || {
  echo "[ssh-runtime] cannot resolve Windows %USERPROFILE% through $WIN_HOST: $WIN_PROFILE" >&2
  exit 1
}
[[ "$WIN_TEMP" =~ ^[A-Za-z]:\\ ]] || {
  echo "[ssh-runtime] cannot resolve Windows %TEMP% through $WIN_HOST: $WIN_TEMP" >&2
  exit 1
}

ENV_DIR="${WIN_PROFILE}\\.rtl87-flash"
WIN_APP_FLASH="${ENV_DIR}\\flash-win32-app.bat"
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

REMOTE_STATE="$(ssh -o BatchMode=yes "$WIN_HOST" "if exist \"$WIN_APP_FLASH\" (if exist \"$WIN_RUNTIME_FLASH\" (if exist \"$WIN_MPCLI\" (if exist \"$WIN_MARKER\" (type \"$WIN_MARKER\") else (echo MISSING)) else (echo MISSING)) else (echo MISSING)) else (echo MISSING)" | tr -d '\r' | head -n 1)"

if [ "$REMOTE_STATE" != "$SIGNATURE" ]; then
  echo "[ssh-runtime] provisioning Windows environment"
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
  echo "[ssh-runtime] Windows environment ready"
else
  echo "[ssh-runtime] Windows environment cached"
fi

echo "[ssh-runtime] host = $WIN_HOST   port = $PORT"
echo "[ssh-runtime] chip = $CHIP   baud = $BAUD"

remote_cmd="set \"CHIP=${CHIP}\" && set \"BAUD=${BAUD}\" && set \"NOPAUSE=1\" && call \"${WIN_RUNTIME_FLASH}\" ${PORT}"
set +e
ssh "$WIN_HOST" "$remote_cmd"
RC=$?
set -e

if [ "$RC" -eq 0 ]; then
  echo "[ssh-runtime] runtime done; it does not include the app image"
  echo "[ssh-runtime] flash the app next with scripts/flash-linux2win-app.sh $PORT"
else
  echo "[ssh-runtime] runtime flash failed on $WIN_HOST" >&2
fi
exit "$RC"
