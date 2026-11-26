#!/usr/bin/env bash
#
# flash-macos-runtime.sh -- flash the complete bundled runtime firmware with mpcli on macOS.
#
# Differences from flash-macos-app.sh:
#   - flash-macos-app.sh flashes a single App image (-p -A 0x7009E000 -F applications/bin/app.bin).
#   - This script flashes the whole runtime (boot patch / upperstack / OTA header /
#     stack & sys patch / DSP sys+app+cfg / SYSTEM & APP Config, 11 images in total),
#     using mpcli's --flash-runtime to generate flash_image.json in one command, then -a flash all.
#
# The runtime does **not include the APP** (0x7009E000 is not in the address table); full flashing order:
#   1. scripts/flash-macos-runtime.sh      # flash the runtime first
#   2. scripts/flash-macos-app.sh          # then flash applications/bin/app.bin
#
# Usage:
#   scripts/flash-macos-runtime.sh                      # auto-detect the download port
#   scripts/flash-macos-runtime.sh /dev/cu.usbserial-1420   # specify the download port
#   scripts/flash-macos-runtime.sh --port /dev/cu.usbserial-1420 --chip RTL8783G
#   PORT=/dev/cu.wchusbserial1420 BAUD=3000000 scripts/flash-macos-runtime.sh
#   CHIP=RTL8783G scripts/flash-macos-runtime.sh        # switch chip
#   DRY=1 scripts/flash-macos-runtime.sh                # only print the command to run
#
# Note: mpcli's --flash-runtime fetches firmware from get_root_path()/fw/runtime_system_bin/<CHIP>.
#       In the packaged build get_root_path() returns sys._MEIPASS (the binary's internal unpack directory), and the official
#       spec already bundles fw/ and config/, so mpcli can flash the runtime no matter where it sits, with no extra
#       fw/ needed. Just do not place an incomplete fw/ next to it (it would shadow the built-in one); the script checks this early.
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_DIR="$SCRIPT_DIR/tool/mpcli"

die() { echo "[flash-runtime] $*" >&2; exit 1; }

[ "$(uname -s)" = "Darwin" ] || {
  echo "[flash-runtime] 警告: 本脚本是给 macOS 用的，当前是 $(uname -s)；Linux 请用 flash-runtime-linux.sh" >&2; }

# ---- Argument parsing ----
# Priority: command line > environment variables > defaults.
#   Named: --port <dev> / --chip <RTL8773G> / --baud <n> (also -c/-b)
#   Positional: /dev/... is a serial port, a value starting with RTL is a chip name, anything else errors.
# Note: the runtime is a fixed set of images plus a fixed address table, with no --fw / --addr.
PORT="${PORT:-}"
# CHIP is the directory name under fw/runtime_system_bin/: RTL8773G / RTL8783G / RTL87X3EP
CHIP="${CHIP:-RTL8773G}"
BAUD="${BAUD:-2000000}"

_usage() {
  die "${2:-未知参数: $1}
  用法: $(basename "${BASH_SOURCE[0]}") [--port <dev>] [--chip <RTL8773G|RTL8783G|RTL87X3EP>] [--baud <n>] [<dev>] [<chip>]
  说明: runtime 烧的是固定 11 个镜像 + 固定地址表，不支持 --fw / --addr；
        要单烧某个镜像请用 flash-macos-app.sh --fw <bin> --addr <0x...>"
}
_needval() { _usage "$1" "参数 $1 缺少取值"; }

while [ $# -gt 0 ]; do
  case "$1" in
    --port|-c)  [ $# -ge 2 ] || _needval "$1"; PORT="$2"; shift 2 ;;
    --port=*)   PORT="${1#*=}"; shift ;;
    --chip)     [ $# -ge 2 ] || _needval "$1"; CHIP="$2"; shift 2 ;;
    --chip=*)   CHIP="${1#*=}"; shift ;;
    --baud|-b)  [ $# -ge 2 ] || _needval "$1"; BAUD="$2"; shift 2 ;;
    --baud=*)   BAUD="${1#*=}"; shift ;;
    --fw|--addr|-f|-a)
                _usage "$1（runtime 不支持自定义镜像/地址）" ;;
    -h|--help)  sed -n '3,30p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
    --*)        _usage "$1" ;;
    *)
      case "$1" in
        /dev/*)    PORT="$1" ;;
        RTL*)      CHIP="$1" ;;
        *)         _usage "$1" ;;
      esac
      shift ;;
  esac
done

# ---- Tool path: like the other platform scripts, defaults to the repo-bundled tool/mpcli/ ----
MPCLI="${MPCLI:-$TOOL_DIR/mpcli-macos}"

[ -f "$MPCLI" ] || die "mpcli 不存在: $MPCLI（macOS 版应放在 $TOOL_DIR/mpcli-macos，或用 MPCLI= 覆盖）"
[ -x "$MPCLI" ] || die "mpcli 不可执行: $MPCLI（chmod +x \"$MPCLI\"）"

if command -v lipo >/dev/null 2>&1 && [ "$(uname -m)" = "arm64" ]; then
  _archs="$(lipo -archs "$MPCLI" 2>/dev/null || true)"
  case "$_archs" in
    *arm64*) ;;
    "") ;;
    *) echo "[flash-runtime] 警告: $MPCLI 只有 $_archs slice，需要 Rosetta 2：sudo softwareupdate --install-rosetta" >&2 ;;
  esac
fi

# ---- Firmware directory: only a "pre-check", not enforced ----
# mpcli's --flash-runtime fetches firmware from get_root_path()/fw/runtime_system_bin/<CHIP>,
# and get_root_path() returns sys._MEIPASS in the packaged build -- i.e. the binary's **internal** unpack directory.
# The official build (including the bundled v4.0.0.7) already has fw/ packed into the executable, so not finding the directory here
# **does not mean it cannot flash**. Only bail out early when "there really is a fw/ next to it but this CHIP is missing"; the rest is left to mpcli.
FW_ROOT="$(dirname "$MPCLI")/fw/runtime_system_bin"
if [ -d "$FW_ROOT" ] && [ ! -d "$FW_ROOT/$CHIP" ]; then
  die "mpcli 旁边有 fw/ 但没有这个芯片目录: $FW_ROOT/$CHIP
  当前可选: $(ls "$FW_ROOT" | tr '\n' ' ')
  若你的 mpcli 是内置 fw 的单文件构建，把本脚本指向它本身即可（不要在旁边放残缺的 fw/）"
fi

# ---- Serial port resolution: positional argument / PORT takes precedence, otherwise auto-detect ----
usb_ports() {
  for _p in /dev/cu.usbserial* /dev/cu.wchusbserial* /dev/cu.SLAB_USBtoUART* \
            /dev/cu.usbmodem*; do
    [ -e "$_p" ] && printf '%s\n' "$_p"
  done | sort -u
  return 0   # when nothing matches, the [ -e ] above exits 1 and, with set -e, silently kills the script
}

if [ -z "$PORT" ]; then
  _ports="$(usb_ports)"
  if [ -z "$_ports" ]; then
    if [ "${DRY:-0}" = "1" ]; then
      PORT="/dev/cu.usbserial-dryrun"
    else
      die "没检测到 USB 串口（ls /dev/cu.* 看看）。设备插好了吗？"
    fi
  else
    _n="$(printf '%s\n' "$_ports" | wc -l | tr -d ' ')"
    if [ "$_n" = "1" ]; then
      PORT="$(printf '%s\n' "$_ports" | head -1)"
      echo "[flash-runtime] 自动选用唯一串口 $PORT"
    else
      die "检测到多个串口，用位置参数或 PORT= 指定：
$(printf '       %s\n' $_ports)"
    fi
  fi
fi

echo "[flash-runtime] mpcli = $MPCLI"
echo "[flash-runtime] port  = $PORT   chip = $CHIP   baud = $BAUD"

# mpcli writes its log next to the executable; if that is not writable it crashes at startup, so fall back to MPCLI_LOG_DIR.
if [ -z "${MPCLI_LOG_DIR:-}" ] && [ ! -w "$(dirname "$MPCLI")" ]; then
  MPCLI_LOG_DIR="$SCRIPT_DIR/log"
  mkdir -p "$MPCLI_LOG_DIR"
  echo "[flash-runtime] $MPCLI 所在目录不可写，log 改写到 MPCLI_LOG_DIR=$MPCLI_LOG_DIR"
fi
export MPCLI_LOG_DIR

# -u -d must be given explicitly: mpcli's --flash-runtime only injects -M 5 -r -b 3000000,
# but SYSTEM_Config lands in the protected 0x70002000 (OEM_CFG region), and without -u it cannot be written.
# -T is derived automatically from CHIP (RTL8773G -> RTL87X3G), no need to pass it.
set -- --flash-runtime "$CHIP" -c "$PORT" -u -d -b "$BAUD"

if [ "${DRY:-0}" = "1" ]; then
  printf '[flash-runtime][dry-run] %q ' "$MPCLI"; printf '%q ' "$@"; echo
  exit 0
fi

exec "$MPCLI" "$@"
