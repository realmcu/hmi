#!/usr/bin/env bash
#
# flash-macos-app.sh -- flash the RTL87X3G App image with mpcli on macOS.
#
# The only difference from flash-linux-app.sh / flash-wsl-app.sh is the execution environment:
#   - Uses the macOS mpcli (Mach-O) from the same directory, i.e.,
#     scripts/tool/mpcli/mpcli-macos, alongside mpcli (Linux ELF) and mpcli.exe (Windows).
#     The default path points to it, no extra flag needed; use MPCLI= to override.
#   - The serial port is /dev/cu.* (callout). **Do not use /dev/tty.\***: its open() waits for the DCD
#     carrier, so when the chip is not up the script hangs silently (looks like a freeze). Passing tty.* is auto-switched to cu.*.
#   - Without a port it auto-detects: a single USB serial port is selected, multiple ports error out to avoid flashing the wrong board.
#   - macOS has no dialout group, serial ports are usually directly readable/writable; the first run may be blocked by Gatekeeper,
#     see the xattr / Rosetta notes in the "macOS flashing" section of the README.
#
# Flash parameters are identical to the other platforms:
#   - Firmware: bin/app.bin (no MP header)
#   - Flash: -c <port> -T RTL87X3G -M 5 -p -A 0x7009E000 -F <fw> -b 2000000 -r -u -d
#
# Usage:
#   scripts/flash-macos-app.sh                              # auto-detect the download port
#   scripts/flash-macos-app.sh /dev/cu.usbserial-1420       # specify the download port
#   scripts/flash-macos-app.sh --fw /path/app.bin --addr 0x7000A000
#   scripts/flash-macos-app.sh /path/app.bin 0x7000A000 /dev/cu.usbserial-1420
#   PORT=/dev/cu.wchusbserial1420 BAUD=3000000 scripts/flash-macos-app.sh
#   DRY=1 scripts/flash-macos-app.sh                        # only print the command to run
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TOOL_DIR="$SCRIPT_DIR/tool/mpcli"

die() { echo "[flash] $*" >&2; exit 1; }

# The most common pitfall when opening fails: mpcli only recognizes "executable" and reports both "missing" and "not executable" vaguely.
[ "$(uname -s)" = "Darwin" ] || {
  echo "[flash] 警告: 本脚本是给 macOS 用的，当前是 $(uname -s)；Linux 请用 flash-linux-app.sh" >&2; }

# ---- Argument parsing ----
# Priority: command line > environment variables > defaults.
# Two forms are supported:
#   Named: --fw <path> / --addr <0x...> / --port <dev> / --baud <n> (also -f/-a/-c/-b)
#   Positional: classified by content, any order -- a leading /dev/ is a serial port, 0x... is an address,
#         anything else is a firmware path. The old form "first positional = serial port" still works.
PORT="${PORT:-}"
FW="${FW:-}"
ADDR="${ADDR:-}"
BAUD="${BAUD:-2000000}"

_usage() {
  die "${2:-未知参数: $1}
  用法: $(basename "${BASH_SOURCE[0]}") [--fw <bin>] [--addr <0xADDR>] [--port <dev>] [--baud <n>] [<bin>] [<addr>] [<dev>]"
}
_needval() { _usage "$1" "参数 $1 缺少取值"; }

while [ $# -gt 0 ]; do
  case "$1" in
    --fw|-f)     [ $# -ge 2 ] || _needval "$1"; FW="$2";   shift 2 ;;
    --fw=*)      FW="${1#*=}";   shift ;;
    --addr|-a)   [ $# -ge 2 ] || _needval "$1"; ADDR="$2"; shift 2 ;;
    --addr=*)    ADDR="${1#*=}"; shift ;;
    --port|-c)   [ $# -ge 2 ] || _needval "$1"; PORT="$2"; shift 2 ;;
    --port=*)    PORT="${1#*=}"; shift ;;
    --baud|-b)   [ $# -ge 2 ] || _needval "$1"; BAUD="$2"; shift 2 ;;
    --baud=*)    BAUD="${1#*=}"; shift ;;
    -h|--help)   sed -n '3,26p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
    --*)         _usage "$1" ;;
    *)
      # Positional arguments are classified by content, independent of order
      case "$1" in
        /dev/*)        PORT="$1" ;;
        0x*|0X*)       ADDR="$1" ;;
        *)             FW="$1" ;;
      esac
      shift ;;
  esac
done

FW="${FW:-$REPO_ROOT/bin/app.bin}"
ADDR="${ADDR:-0x7009E000}"

# ---- Tool path: like the other platform scripts, defaults to the repo-bundled tool/mpcli/ ----
MPCLI="${MPCLI:-$TOOL_DIR/mpcli-macos}"

[ -f "$MPCLI" ] || die "mpcli 不存在: $MPCLI（macOS 版应放在 $TOOL_DIR/mpcli-macos，或用 MPCLI= 覆盖）"
[ -x "$MPCLI" ] || die "mpcli 不可执行: $MPCLI（chmod +x \"$MPCLI\"）"

# Running x86_64 mpcli on Apple Silicon needs Rosetta 2, or it reports "Bad CPU type in executable"
if command -v lipo >/dev/null 2>&1 && [ "$(uname -m)" = "arm64" ]; then
  _archs="$(lipo -archs "$MPCLI" 2>/dev/null || true)"
  case "$_archs" in
    *arm64*) ;;
    "") ;;
    *) echo "[flash] 警告: $MPCLI 只有 $_archs slice，需要 Rosetta 2：sudo softwareupdate --install-rosetta" >&2 ;;
  esac
fi

# ---- Serial port resolution: positional argument / PORT takes precedence, otherwise auto-detect ----
usb_ports() {
  for _p in /dev/cu.usbserial* /dev/cu.wchusbserial* /dev/cu.SLAB_USBtoUART* \
            /dev/cu.usbmodem*; do
    [ -e "$_p" ] && printf '%s\n' "$_p"
  done | sort -u
  return 0   # when nothing matches, the [ -e ] above exits 1 and, with set -e, silently kills the script
}

if [ -n "$PORT" ]; then
  case "$PORT" in
    /dev/tty.*)
      echo "[flash] 提示: $PORT 是 tty.* 设备，open() 会等 DCD 载波，可能静默挂住；改用 cu.*" >&2
      PORT="/dev/cu.${PORT#/dev/tty.}"
      ;;
  esac
else
  _ports="$(usb_ports)"
  if [ -z "$_ports" ]; then
    if [ "${DRY:-0}" = "1" ]; then
      PORT="/dev/cu.usbserial-dryrun"
    else
      die "没检测到 USB 串口（ls /dev/cu.* 看看）。设备插好了吗？数据线是能传数据的吗？"
    fi
  else
    _n="$(printf '%s\n' "$_ports" | wc -l | tr -d ' ')"
    if [ "$_n" = "1" ]; then
      PORT="$(printf '%s\n' "$_ports" | head -1)"
      echo "[flash] 自动选用唯一串口 $PORT"
    else
      die "检测到多个串口，用位置参数或 PORT= 指定：
$(printf '       %s\n' $_ports)"
    fi
  fi
fi

# ---- Checks ----
[ -f "$FW" ] || die "固件不存在: $FW（先 west build 生成 bin/app.bin）"
# Convert to an absolute path: the script never changes CWD, but an absolute path avoids ambiguity if the caller cd'd elsewhere.
FW="$(cd "$(dirname "$FW")" && pwd)/$(basename "$FW")"

if [ "${DRY:-0}" != "1" ]; then
  [ -e "$PORT" ] || die "串口不存在: $PORT（检查 USB 连接，或 ls /dev/cu.*）"
  if [ ! -r "$PORT" ] || [ ! -w "$PORT" ]; then die "串口不可读写: $PORT（ls -l 看权限，或换个 cu.* 设备）"; fi
fi

echo "[flash] mpcli = $MPCLI"
echo "[flash] port  = $PORT   addr = $ADDR   baud = $BAUD"
echo "[flash] fw    = $FW"

# mpcli writes its log **next to the executable** (log.py uses the sys.executable directory),
# and if that location is not writable it crashes with PermissionError at startup. A common pitfall when placed in /usr/local/bin etc.,
# use MPCLI_LOG_DIR to move it somewhere writable (when set, it no longer creates date subdirectories).
if [ -z "${MPCLI_LOG_DIR:-}" ] && [ ! -w "$(dirname "$MPCLI")" ]; then
  MPCLI_LOG_DIR="$SCRIPT_DIR/log"
  mkdir -p "$MPCLI_LOG_DIR"
  echo "[flash] $MPCLI 所在目录不可写，log 改写到 MPCLI_LOG_DIR=$MPCLI_LOG_DIR"
fi
export MPCLI_LOG_DIR

set -- -c "$PORT" -T RTL87X3G -M 5 -p -A "$ADDR" -F "$FW" -b "$BAUD" -r -u -d

if [ "${DRY:-0}" = "1" ]; then
  printf '[flash][dry-run] %q ' "$MPCLI"; printf '%q ' "$@"; echo
  exit 0
fi

exec "$MPCLI" "$@"
