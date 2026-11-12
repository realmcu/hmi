#!/usr/bin/env bash
#
# flash-macos-app.sh —— 在 macOS 下用 mpcli 烧录 RTL87X3G 的 App 镜像。
#
# 与 flash-linux-app.sh / flash-wsl-app.sh 的差别只在执行环境：
#   - 工具用同一个目录下的 macOS 版 mpcli（Mach-O），即
#     scripts/tool/mpcli/mpcli-macos，与 mpcli（Linux ELF）、mpcli.exe（Windows）
#     并排。默认路径就指向它，不需要额外指定；要换位置用 MPCLI= 覆盖。
#   - 串口是 /dev/cu.*（callout）。**不要用 /dev/tty.\***：它的 open() 会等 DCD
#     载波，芯片没起来时脚本会静默挂住（看着像卡死）。传进 tty.* 会自动换成 cu.*。
#   - 不传端口时自动探测：只有一个 USB 串口就选它，多个就报错让你指定（防烧错板子）。
#   - macOS 没有 dialout 组，串口通常直接可读写；首次运行可能被 Gatekeeper 拦，
#     见 README「macOS 烧录」一节的 xattr / Rosetta 说明。
#
# 烧录参数与其它平台逐字一致：
#   - 固件：bin/app.bin（无 MP 头）
#   - 烧录：-c <port> -T RTL87X3G -M 5 -p -A 0x7009E000 -F <fw> -b 2000000 -r -u -d
#
# 用法：
#   scripts/flash-macos-app.sh                              # 自动探测下载口
#   scripts/flash-macos-app.sh /dev/cu.usbserial-1420       # 指定下载口
#   scripts/flash-macos-app.sh --fw /path/app.bin --addr 0x7000A000
#   scripts/flash-macos-app.sh /path/app.bin 0x7000A000 /dev/cu.usbserial-1420
#   PORT=/dev/cu.wchusbserial1420 BAUD=3000000 scripts/flash-macos-app.sh
#   DRY=1 scripts/flash-macos-app.sh                        # 只打印将要执行的命令
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TOOL_DIR="$SCRIPT_DIR/tool/mpcli"

die() { echo "[flash] $*" >&2; exit 1; }

# 打开失败时最容易踩的坑：mpcli 只认「可执行」，「不存在/不可执行」都报得含糊。
[ "$(uname -s)" = "Darwin" ] || {
  echo "[flash] 警告: 本脚本是给 macOS 用的，当前是 $(uname -s)；Linux 请用 flash-linux-app.sh" >&2; }

# ---- 参数解析 ----
# 优先级：命令行 > 环境变量 > 默认值。
# 支持两种写法：
#   命名：--fw <path> / --addr <0x...> / --port <dev> / --baud <n>（也认 -f/-a/-c/-b）
#   位置：按内容自动归类，顺序随意 —— 以 /dev/ 开头的是串口，0x... 是地址，
#         其余当固件路径。老写法「第一个位置参数 = 串口」仍然可用。
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
      # 位置参数按内容归类，不依赖顺序
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

# ---- 工具路径：和其它平台脚本一样，默认取仓库自带的 tool/mpcli/ ----
MPCLI="${MPCLI:-$TOOL_DIR/mpcli-macos}"

[ -f "$MPCLI" ] || die "mpcli 不存在: $MPCLI（macOS 版应放在 $TOOL_DIR/mpcli-macos，或用 MPCLI= 覆盖）"
[ -x "$MPCLI" ] || die "mpcli 不可执行: $MPCLI（chmod +x \"$MPCLI\"）"

# Apple Silicon 上跑 x86_64 的 mpcli 需要 Rosetta 2，缺了会报 "Bad CPU type in executable"
if command -v lipo >/dev/null 2>&1 && [ "$(uname -m)" = "arm64" ]; then
  _archs="$(lipo -archs "$MPCLI" 2>/dev/null || true)"
  case "$_archs" in
    *arm64*) ;;
    "") ;;
    *) echo "[flash] 警告: $MPCLI 只有 $_archs slice，需要 Rosetta 2：sudo softwareupdate --install-rosetta" >&2 ;;
  esac
fi

# ---- 串口解析：位置参数 / PORT 优先，否则自动探测 ----
usb_ports() {
  for _p in /dev/cu.usbserial* /dev/cu.wchusbserial* /dev/cu.SLAB_USBtoUART* \
            /dev/cu.usbmodem*; do
    [ -e "$_p" ] && printf '%s\n' "$_p"
  done | sort -u
  return 0   # 一个都没匹配到时上面的 [ -e ] 会以 1 退出，配合 set -e 会静默杀脚本
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

# ---- 检查 ----
[ -f "$FW" ] || die "固件不存在: $FW（先 west build 生成 bin/app.bin）"
# 转成绝对路径：脚本全程不切 CWD，但绝对路径能避免调用方在别处 cd 过的歧义。
FW="$(cd "$(dirname "$FW")" && pwd)/$(basename "$FW")"

if [ "${DRY:-0}" != "1" ]; then
  [ -e "$PORT" ] || die "串口不存在: $PORT（检查 USB 连接，或 ls /dev/cu.*）"
  if [ ! -r "$PORT" ] || [ ! -w "$PORT" ]; then die "串口不可读写: $PORT（ls -l 看权限，或换个 cu.* 设备）"; fi
fi

echo "[flash] mpcli = $MPCLI"
echo "[flash] port  = $PORT   addr = $ADDR   baud = $BAUD"
echo "[flash] fw    = $FW"

# mpcli 的 log 写在 **可执行文件同目录**（log.py 用 sys.executable 目录），
# 那里不可写会在启动时直接 PermissionError 崩掉。放 /usr/local/bin 之类的常见坑，
# 用 MPCLI_LOG_DIR 把它挪到可写位置（设了就不再按日期建子目录）。
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
