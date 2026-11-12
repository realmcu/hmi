#!/usr/bin/env bash
#
# flash-macos-runtime.sh —— 在 macOS 下用 mpcli 烧录随包自带的整套 runtime 固件。
#
# 与 flash-macos-app.sh 的区别：
#   - flash-macos-app.sh 烧的是单个 App 镜像（-p -A 0x7009E000 -F bin/app.bin）。
#   - 本脚本烧的是整套 runtime（boot patch / upperstack / OTA header /
#     stack & sys patch / DSP sys+app+cfg / SYSTEM & APP Config，共 11 个镜像），
#     由 mpcli 的 --flash-runtime 一条命令生成 flash_image.json 后 -a 全刷。
#
# runtime 里【不含 APP】（地址表里没有 0x7009E000），完整刷机顺序：
#   1. scripts/flash-macos-runtime.sh      # 先烧 runtime
#   2. scripts/flash-macos-app.sh          # 再烧 bin/app.bin
#
# 用法：
#   scripts/flash-macos-runtime.sh                      # 自动探测下载口
#   scripts/flash-macos-runtime.sh /dev/cu.usbserial-1420   # 指定下载口
#   scripts/flash-macos-runtime.sh --port /dev/cu.usbserial-1420 --chip RTL8783G
#   PORT=/dev/cu.wchusbserial1420 BAUD=3000000 scripts/flash-macos-runtime.sh
#   CHIP=RTL8783G scripts/flash-macos-runtime.sh        # 换芯片
#   DRY=1 scripts/flash-macos-runtime.sh                # 只打印将要执行的命令
#
# 备注：mpcli 的 --flash-runtime 从 get_root_path()/fw/runtime_system_bin/<CHIP> 取固件。
#       打包版里 get_root_path() 返回 sys._MEIPASS（二进制内部的解包目录），而官方
#       spec 已把 fw/、config/ 打进去，所以 mpcli 单独放哪都能烧 runtime，不必额外准备
#       fw/。只要别在它旁边放一个残缺的 fw/（会盖住内置那份），脚本会提前拦。
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_DIR="$SCRIPT_DIR/tool/mpcli"

die() { echo "[flash-runtime] $*" >&2; exit 1; }

[ "$(uname -s)" = "Darwin" ] || {
  echo "[flash-runtime] 警告: 本脚本是给 macOS 用的，当前是 $(uname -s)；Linux 请用 flash-runtime-linux.sh" >&2; }

# ---- 参数解析 ----
# 优先级：命令行 > 环境变量 > 默认值。
#   命名：--port <dev> / --chip <RTL8773G> / --baud <n>（也认 -c/-b）
#   位置：/dev/... 当串口，以 RTL 开头的当芯片名，其余报错。
# 注意：runtime 是固定的一套镜像 + 固定地址表，没有 --fw / --addr。
PORT="${PORT:-}"
# CHIP 取 fw/runtime_system_bin/ 下的目录名：RTL8773G / RTL8783G / RTL87X3EP
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

# ---- 工具路径：和其它平台脚本一样，默认取仓库自带的 tool/mpcli/ ----
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

# ---- 固件目录：只是「预检」，不强制 ----
# mpcli 的 --flash-runtime 从 get_root_path()/fw/runtime_system_bin/<CHIP> 取固件，
# 而 get_root_path() 在打包版里返回 sys._MEIPASS —— 也就是二进制**内部**的解包目录。
# 官方构建（含本仓库自带的 v4.0.0.7）已把 fw/ 打进可执行文件，所以这里查不到目录
# 【不代表烧不了】。只在「旁边确实有个 fw/ 但缺这个 CHIP」时才提前拦，其余交给 mpcli。
FW_ROOT="$(dirname "$MPCLI")/fw/runtime_system_bin"
if [ -d "$FW_ROOT" ] && [ ! -d "$FW_ROOT/$CHIP" ]; then
  die "mpcli 旁边有 fw/ 但没有这个芯片目录: $FW_ROOT/$CHIP
  当前可选: $(ls "$FW_ROOT" | tr '\n' ' ')
  若你的 mpcli 是内置 fw 的单文件构建，把本脚本指向它本身即可（不要在旁边放残缺的 fw/）"
fi

# ---- 串口解析：位置参数 / PORT 优先，否则自动探测 ----
usb_ports() {
  for _p in /dev/cu.usbserial* /dev/cu.wchusbserial* /dev/cu.SLAB_USBtoUART* \
            /dev/cu.usbmodem*; do
    [ -e "$_p" ] && printf '%s\n' "$_p"
  done | sort -u
  return 0   # 一个都没匹配到时上面的 [ -e ] 会以 1 退出，配合 set -e 会静默杀脚本
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

# mpcli 的 log 写在可执行文件同目录；不可写会启动即崩，用 MPCLI_LOG_DIR 兜底。
if [ -z "${MPCLI_LOG_DIR:-}" ] && [ ! -w "$(dirname "$MPCLI")" ]; then
  MPCLI_LOG_DIR="$SCRIPT_DIR/log"
  mkdir -p "$MPCLI_LOG_DIR"
  echo "[flash-runtime] $MPCLI 所在目录不可写，log 改写到 MPCLI_LOG_DIR=$MPCLI_LOG_DIR"
fi
export MPCLI_LOG_DIR

# -u -d 必须显式给：mpcli 的 --flash-runtime 只注入 -M 5 -r -b 3000000，
# 而 SYSTEM_Config 落在受保护的 0x70002000（OEM_CFG 区），少了 -u 写不进去。
# -T 由 CHIP 自动推导（RTL8773G -> RTL87X3G），不用传。
set -- --flash-runtime "$CHIP" -c "$PORT" -u -d -b "$BAUD"

if [ "${DRY:-0}" = "1" ]; then
  printf '[flash-runtime][dry-run] %q ' "$MPCLI"; printf '%q ' "$@"; echo
  exit 0
fi

exec "$MPCLI" "$@"
