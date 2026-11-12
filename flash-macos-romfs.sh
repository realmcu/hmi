#!/usr/bin/env bash
#
# flash-macos-romfs.sh —— 在 macOS 下用 mpcli 烧录 HoneyGUI romfs 资源镜像到 USER_DATA 区。
#
# 与 flash-macos-app.sh 的区别：那个烧 App（bin/app.bin → 0x7009E000，写死），
# 本脚本烧资源镜像，镜像和地址由调用方给（地址能从文件名里自动取）。
#
# 地址取 flash_map.h 的分区地址（本 App 的 USER_DATA1_ADDR = 0x70536000；换 App 目录
# 先 grep 一下，不同工程的 flash_map.h 不一样），必须与 ui/project.json 的
# romfsBaseAddr、gui_vfs_mount_romfs() 调用一致 —— romfs 指针按基址烘焙成绝对
# 地址，烧错地址不报错，只在运行时挂载失败。地址还须 4KB 扇区对齐，否则 mpcli
# 擦除起点上调但长度不减，头部若干字节未擦除导致 flash verify 失败（status 52）。
# 两点都会检查，外加 romfs root dirent 是否真在该地址解析。镜像是 mkromfs 的裸
# 产物，不带 MP header，直接烧。
#
# 用法：
#   scripts/flash-macos-romfs.sh <bin> [addr] [port]
#
#   scripts/flash-macos-romfs.sh ../ui/build/app_romfs_0x70536000.bin
#   scripts/flash-macos-romfs.sh ../ui/build/app_romfs_0x70536000.bin 0x70536000
#   scripts/flash-macos-romfs.sh ../ui/build/app_romfs_0x70536000.bin 0x70536000 /dev/cu.usbserial-1420
#   scripts/flash-macos-romfs.sh --fw <bin> --addr 0x70536000 --port /dev/cu.usbserial-1420
#   PORT=/dev/cu.wchusbserial1420 BAUD=3000000 scripts/flash-macos-romfs.sh <bin>
#   DRY=1 scripts/flash-macos-romfs.sh <bin>            # 只打印命令，不真正烧录
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_DIR="$SCRIPT_DIR/tool/mpcli"
SECTOR=4096

die() { echo "[flash-romfs] $*" >&2; exit 1; }

[ "$(uname -s)" = "Darwin" ] || {
  echo "[flash-romfs] 警告: 本脚本是给 macOS 用的，当前是 $(uname -s)" >&2; }

# ---- 参数解析 ----
# 优先级：命令行 > 环境变量 > 默认值。FW 必填，ADDR 可从文件名里的 0x... 推导。
#   命名：--fw <bin> / --addr <0x...> / --port <dev> / --baud <n>（也认 -f/-a/-c/-b）
#   位置：按内容自动归类，顺序随意 —— /dev/... 是串口，0x... 是地址，其余当镜像路径。
FW="${FW:-}"
ADDR="${ADDR:-}"
PORT="${PORT:-}"
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
    -h|--help)   sed -n '3,30p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
    --*)         _usage "$1" ;;
    *)
      case "$1" in
        /dev/*)    PORT="$1" ;;
        0x*|0X*)   ADDR="$1" ;;
        *)         FW="$1" ;;
      esac
      shift ;;
  esac
done

# ---- 工具路径：和其它平台脚本一样，默认取仓库自带的 tool/mpcli/ ----
MPCLI="${MPCLI:-$TOOL_DIR/mpcli-macos}"

[ -f "$MPCLI" ] || die "mpcli 不存在: $MPCLI（macOS 版应放在 $TOOL_DIR/mpcli-macos，或用 MPCLI= 覆盖）"
[ -x "$MPCLI" ] || die "mpcli 不可执行: $MPCLI（chmod +x \"$MPCLI\"）"

# ---- 镜像与地址 ----
if [ -z "$FW" ]; then
  me="$(basename "${BASH_SOURCE[0]}")"
  echo "[flash-romfs] 用法: $me <bin> [addr] [port]" >&2
  echo "[flash-romfs]   例: $me ../ui/build/app_romfs_0x70536000.bin" >&2
  exit 1
fi
[ -f "$FW" ] || die "镜像不存在: $FW"
# 转成绝对路径：脚本全程不切 CWD，但绝对路径能避免调用方在别处 cd 过的歧义。
FW="$(cd "$(dirname "$FW")" && pwd)/$(basename "$FW")"

if [ -z "$ADDR" ]; then
  ADDR="$(basename "$FW" | grep -oiE '0x[0-9a-f]{6,8}' | head -1 || true)"
  [ -n "$ADDR" ] || die "无法从文件名推导基址，请显式给 <addr>（取 flash_map.h 的分区地址）"
  echo "[flash-romfs] 从文件名取到基址 $ADDR"
fi

[[ "$ADDR" =~ ^0[xX][0-9a-fA-F]+$ ]] || die "地址格式不对: $ADDR（需 0x 开头的十六进制）"
[ $(( $((ADDR)) % SECTOR )) -eq 0 ] || {
  die "地址 $ADDR 不是 4KB 扇区对齐，烧录会 verify 失败；请用 flash_map.h 的分区地址"; }

# Designer 把基址写进文件名，校验它与 <addr> 一致
FW_BASE="$(basename "$FW" | grep -oiE '0x[0-9a-f]{6,8}' | head -1 || true)"
[ -z "$FW_BASE" ] || [ "$((FW_BASE))" -eq "$((ADDR))" ] || {
  die "镜像名里的基址 $FW_BASE 与地址 $ADDR 不一致（会挂载失败）"; }

# ---- 校验 romfs root dirent 是否真落在 ADDR ----
# struct romfs_dirent { uint32_t type; const char *name; const uint8_t *data; size_t size; }
# 镜像以 root dirent 开头，name 是绝对指针，故 (name - ADDR) 必须能索引回镜像里
# 并解析出 "/"。这条能抓住「文件名改了但镜像是按别的基址编的」。
if command -v python3 >/dev/null 2>&1; then
  python3 - "$FW" "$ADDR" <<'PY' || die "romfs root dirent 在 $ADDR 解析失败 —— 镜像或地址不对"
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
  echo "[flash-romfs] 没装 python3，跳过 romfs root dirent 校验（xcode-select --install 可装）" >&2
fi

# ---- 串口解析 ----
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
      die "没检测到 USB 串口（ls /dev/cu.* 看看）"
    fi
  else
    _n="$(printf '%s\n' "$_ports" | wc -l | tr -d ' ')"
    if [ "$_n" = "1" ]; then
      PORT="$(printf '%s\n' "$_ports" | head -1)"
      echo "[flash-romfs] 自动选用唯一串口 $PORT"
    else
      die "检测到多个串口，用位置参数或 PORT= 指定：
$(printf '       %s\n' $_ports)"
    fi
  fi
fi

FW_SIZE="$(wc -c < "$FW" | tr -d ' ')"   # wc 比 stat 的可移植性好（BSD/GNU 参数不一样）

echo "[flash-romfs] mpcli = $MPCLI"
echo "[flash-romfs] port  = $PORT   addr = $ADDR   baud = $BAUD"
echo "[flash-romfs] fw    = $FW ($FW_SIZE bytes)"
echo "[flash-romfs] note  = 会整片擦写 $ADDR 处的 romfs 分区"

# mpcli 的 log 写在可执行文件同目录；不可写会启动即崩，用 MPCLI_LOG_DIR 兜底。
if [ -z "${MPCLI_LOG_DIR:-}" ] && [ ! -w "$(dirname "$MPCLI")" ]; then
  MPCLI_LOG_DIR="$SCRIPT_DIR/log"
  mkdir -p "$MPCLI_LOG_DIR"
  echo "[flash-romfs] $MPCLI 所在目录不可写，log 改写到 MPCLI_LOG_DIR=$MPCLI_LOG_DIR"
fi
export MPCLI_LOG_DIR

set -- -c "$PORT" -T RTL87X3G -M 5 -p -A "$ADDR" -F "$FW" -b "$BAUD" -r -u -d

if [ "${DRY:-0}" = "1" ]; then
  printf '[flash-romfs][dry-run] %q ' "$MPCLI"; printf '%q ' "$@"; echo
  exit 0
fi

exec "$MPCLI" "$@"
