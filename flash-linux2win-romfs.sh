#!/usr/bin/env bash
# Flash a HoneyGUI romfs resource image on a Windows workstation reached
# through an SSH alias.
#
# Companion to flash-linux2win-app.sh: that one flashes the application
# (bin/app.bin -> 0x7009E000), this one flashes the UI resources, which are a
# separate image at a separate offset. Both share the same provisioned
# %USERPROFILE%\.rtl87-flash environment, so running one after the other does
# not re-provision.
#
# WHY THE ADDRESS MATTERS
#   mkromfs bakes the base address into every pointer inside the image, and
#   gui_vfs_mount_romfs() reads the image at that same address, and
#   ui/project.json records it as romfsBaseAddr. All three must agree with the
#   flash offset. A mismatch does NOT fail at flash time -- mpcli happily writes
#   the bytes and verifies them -- it fails at boot as
#       open file fail:/img/....bin !  /  gui_img_ctor assertion / USAGE FAULT
#   So this script checks the address three ways before touching the port:
#   sector alignment, the base baked into the image file name, and the romfs
#   root dirent actually resolving at that base.
#
# Notes on the image itself:
#   * It is the raw mkromfs output, NOT wrapped in an MP header, so it is
#     flashed bare at the partition base (USER_DATA1_ADDR in flash_map.h).
#   * The address must be 4KB sector aligned, otherwise mpcli rounds the erase
#     start up while keeping the full length, leaving the head of the range
#     unerased and the verify fails with status 52.
#
# Usage:
#   scripts/flash-linux2win-romfs.sh                      # sole app_romfs_0x*.bin in ../ui/build
#   scripts/flash-linux2win-romfs.sh ../ui/build/app_romfs_0x704D1000.bin
#   scripts/flash-linux2win-romfs.sh <bin> 0x704D1000 COM4
#   WIN_HOST=main-win-pc FW=/path/to.bin ADDR=0x704D1000 scripts/flash-linux2win-romfs.sh
#   DRY=1 scripts/flash-linux2win-romfs.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
UI_ROOT="${UI_ROOT:-$REPO_ROOT/../ui}"
SECTOR=4096

WIN_HOST="${WIN_HOST:-main-win-pc}"
PORT="${3:-${PORT:-COM4}}"
FW="${1:-${FW:-}}"
ADDR="${2:-${ADDR:-}}"
BAUD="${BAUD:-2000000}"
DRY="${DRY:-0}"

die() { echo "[ssh-romfs] $*" >&2; exit 1; }

# ---- locate the image -------------------------------------------------------
# The Designer writes the base address into the file name, so when several
# images exist we refuse to guess rather than silently flashing the wrong one.
if [ -z "$FW" ]; then
  shopt -s nullglob
  _imgs=("$UI_ROOT"/build/app_romfs_0x*.bin)
  shopt -u nullglob
  case ${#_imgs[@]} in
    0) die "no app_romfs_0x*.bin under $UI_ROOT/build -- build the UI, or pass <bin>"
       ;;
    1) FW="${_imgs[0]}" ;;
    *) die "multiple romfs images under $UI_ROOT/build, pass one explicitly:
       $(printf '%s\n       ' "${_imgs[@]}")" ;;
  esac
fi

[ -f "$FW" ] || die "romfs image not found: $FW"

# ---- resolve the base address ----------------------------------------------
if [ -z "$ADDR" ]; then
  ADDR="$(basename "$FW" | grep -oiE '0x[0-9a-f]{6,8}' | head -1 || true)"
  [ -n "$ADDR" ] || die "cannot derive the base address from '$FW' -- pass <addr> explicitly"
fi

[[ "$ADDR" =~ ^0[xX][0-9a-fA-F]+$ ]] || die "bad address '$ADDR' (expected 0x-prefixed hex)"
[ $(( $((ADDR)) % SECTOR )) -eq 0 ] || {
  die "address $ADDR is not 4KB sector aligned; use the partition base from flash_map.h"; }

# The Designer puts the base in the name; a mismatch means the image was built
# for a different mount point than the one being flashed to.
FW_BASE="$(basename "$FW" | grep -oiE '0x[0-9a-f]{6,8}' | head -1 || true)"
if [ -n "$FW_BASE" ] && [ "$((FW_BASE))" -ne "$((ADDR))" ]; then
  die "image name says $FW_BASE but flashing to $ADDR -- the baked pointers would be wrong"
fi

# ---- verify the romfs root dirent actually lands at ADDR --------------------
# struct romfs_dirent { uint32_t type; const char *name; const uint8_t *data; size_t size; }
# The image starts at the root dirent, and `name` is an absolute pointer, so
# (name - ADDR) must index into the file and resolve to "/". This is the check
# that catches an image built for a different base even when the file name was
# renamed to match.
if command -v python3 >/dev/null 2>&1; then
  python3 - "$FW" "$ADDR" <<'PY' || die "romfs root dirent does not resolve at $ADDR -- wrong image or wrong address"
import struct, sys
img, addr = sys.argv[1], int(sys.argv[2], 16)
data = open(img, 'rb').read()
if len(data) < 16:
    sys.exit("image is too small to be a romfs")
dtype, name_ptr, _data_ptr, size = struct.unpack_from('<IIII', data, 0)
if dtype not in (0, 1):
    sys.exit("root dirent type=%d is neither FILE(0) nor DIR(1)" % dtype)
off = name_ptr - addr
if not (0 <= off < len(data)):
    sys.exit("root name pointer 0x%08X falls outside the image when based at 0x%08X" % (name_ptr, addr))
end = data.find(b'\x00', off)
name = data[off:end if end != -1 else off + 64].decode('latin1', 'replace')
if not name.startswith('/'):
    sys.exit("root name resolves to %r, expected '/'" % name)
print("root dirent OK: type=%d name=%r entries=%d" % (dtype, name, size))
PY
else
  echo "[ssh-romfs] python3 not found; skipping the romfs root dirent check" >&2
fi

FW_SIZE="$(stat -c%s "$FW")"

if [ "$DRY" = "1" ]; then
  echo "[ssh-romfs][dry-run] host = $WIN_HOST   port = $PORT"
  echo "[ssh-romfs][dry-run] fw   = $FW ($FW_SIZE bytes)"
  echo "[ssh-romfs][dry-run] addr = $ADDR   baud = $BAUD"
  echo "[ssh-romfs][dry-run] env  = %USERPROFILE%\\.rtl87-flash"
  echo "[ssh-romfs][dry-run] provision from tool/mpcli and flash-win32-app.bat when missing"
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
WIN_MPCLI="${ENV_DIR}\\tool\\mpcli\\mpcli.exe"
WIN_MARKER="${ENV_DIR}\\.bundle.sha256"

# Identical signature to flash-linux2win-app.sh on purpose: the two scripts
# share one provisioned environment, so whichever runs first installs it and
# the other reuses it.
SIGNATURE="$(
  cd "$SCRIPT_DIR"
  {
    sha256sum flash-win32-app.bat
    find tool/mpcli -type f -print0 | sort -z | xargs -0 sha256sum
  } | sha256sum | awk '{print $1}'
)"

REMOTE_STATE="$(ssh -o BatchMode=yes "$WIN_HOST" "if exist \"$WIN_FLASH\" (if exist \"$WIN_MPCLI\" (if exist \"$WIN_MARKER\" (type \"$WIN_MARKER\") else (echo MISSING)) else (echo MISSING)) else (echo MISSING)" | tr -d '\r' | head -n 1)"

if [ "$REMOTE_STATE" != "$SIGNATURE" ]; then
  echo "[ssh-romfs] provisioning Windows environment"
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
  echo "[ssh-romfs] Windows environment ready"
else
  echo "[ssh-romfs] Windows environment cached"
fi

REMOTE_NAME="rtl87_romfs_$(date +%Y%m%d_%H%M%S)_$$.bin"
REMOTE_WIN_FW="${WIN_TEMP}\\${REMOTE_NAME}"
REMOTE_SCP_FW="${WIN_TEMP//\\//}/${REMOTE_NAME}"

echo "[ssh-romfs] host = $WIN_HOST   port = $PORT"
echo "[ssh-romfs] fw   = $FW ($FW_SIZE bytes)"
echo "[ssh-romfs] addr = $ADDR   baud = $BAUD"
echo "[ssh-romfs] upload = $REMOTE_WIN_FW"
echo "[ssh-romfs] note = this erases and rewrites the whole romfs partition at $ADDR"

scp -O -q "$FW" "${WIN_HOST}:${REMOTE_SCP_FW}"

remote_cmd="set \"FW=${REMOTE_WIN_FW}\" && set \"ADDR=${ADDR}\" && set \"BAUD=${BAUD}\" && set \"NOPAUSE=1\" && call \"${WIN_FLASH}\" ${PORT}"
set +e
ssh "$WIN_HOST" "$remote_cmd"
RC=$?
set -e

if [ "$RC" -eq 0 ]; then
  ssh -o BatchMode=yes "$WIN_HOST" "del /q \"${REMOTE_WIN_FW}\" >nul 2>nul" || true
  echo "[ssh-romfs] done; if the UI still fails to load resources, re-check that"
  echo "[ssh-romfs] ui/project.json romfsBaseAddr and the gui_vfs_mount_romfs() call"
  echo "[ssh-romfs] in ui/src match $ADDR"
else
  echo "[ssh-romfs] flash failed; remote image kept for diagnostics: $REMOTE_WIN_FW" >&2
fi
exit "$RC"
