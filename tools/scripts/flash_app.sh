#!/usr/bin/env bash
# 只烧 app 镜像（不改 romfs / FDB）——在 WSL2 里驱动 Windows 版 mpcli.exe。
#
# 参考 scripts/flash-wsl-app.sh（独立仓库 HoneyRepo/hmi），按本工程布局调整：
#   - 仓库根由 tools/scripts/ 推出，可从任意目录执行
#   - 默认镜像与地址按 --bank 推导，地址取自 flash_map.ini，不写死
#   - 默认镜像为 OTA 产物 bin/bank<N>/app_bank<N>.bin；
#     打包过的完整镜像（app+romfs）用 --fw bin/bank<N>/app_bank<N>_packed.bin
#
# 原理：WSL 直接调用 Windows 的 mpcli.exe（原生进程，能访问 COM 口），
#       用 wslpath -w 把 WSL 里的固件路径转成 Windows 形式喂给 -F。
#
# 用法：
#   ./tools/scripts/flash_app.sh                       # 默认 bank0 + COM5
#   ./tools/scripts/flash_app.sh COM7                  # 位置参数 = 串口
#   ./tools/scripts/flash_app.sh --bank 1
#   ./tools/scripts/flash_app.sh --fw bin/bank0/app_bank0_packed.bin
#   DRY=1 ./tools/scripts/flash_app.sh                 # 只打印命令
#
# 环境变量: MPCLI_EXE FW ADDR PORT BAUD DRY=1
#   MPCLI_EXE 默认 scripts/tool/mpcli/mpcli.exe（与 flash_fdb.sh / flash_romfs.sh 相同）
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"                 # 应用工程根目录 = 仓库根

MPCLI_EXE="${MPCLI_EXE:-$APP_DIR/scripts/tool/mpcli/mpcli.exe}"
FLASH_MAP="$APP_DIR/flash_map.ini"
PORT="${PORT:-COM5}"
BAUD="${BAUD:-3000000}"
BANK=0
FW="${FW:-}"
ADDR="${ADDR:-}"
DRY="${DRY:-0}"

usage() {
    cat <<EOF
用法: $(basename "$0") [--bank 0|1] [--port COM<n>] [--fw FILE] [--addr 0x...]
                       [--baud <n>] [--dry-run]

只烧 app 镜像到已烧好基础固件的设备，不动 romfs / FDB。
默认镜像 bin/bank<N>/app_bank<N>.bin，地址读 flash_map.ini 的 BANK<N>_APP_ADDR。
需要先执行: ./tools/scripts/build.sh --bank <N>

环境变量: MPCLI_EXE FW ADDR PORT BAUD DRY=1
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --bank) BANK="$2"; shift 2 ;;
        --port) PORT="$2"; shift 2 ;;
        --fw)   FW="$2"; shift 2 ;;
        --addr) ADDR="$2"; shift 2 ;;
        --baud) BAUD="$2"; shift 2 ;;
        --dry-run) DRY=1; shift ;;
        -h|--help) usage; exit 0 ;;
        -*) echo "未知参数: $1" >&2; usage; exit 1 ;;
        *)  PORT="$1"; shift ;;      # 位置参数 = 端口
    esac
done

case "$BANK" in
    0|1) ;;
    *) echo "错误: --bank 只能是 0 或 1" >&2; exit 1 ;;
esac

die() { echo "[app-flash] 错误: $*" >&2; exit 1; }

FW="${FW:-$APP_DIR/bin/bank${BANK}/app_bank${BANK}.bin}"

# ---- 烧录地址：默认从 flash_map.ini 读，避免脚本里再写一份常量 ----
if [ -z "$ADDR" ]; then
    [ -f "$FLASH_MAP" ] || die "找不到 flash_map.ini: $FLASH_MAP"
    ADDR=$(grep -E "^BANK${BANK}_APP_ADDR=" "$FLASH_MAP" | cut -d= -f2 | tr -d '[:space:]' || true)
    [ -n "$ADDR" ] || die "flash_map.ini 里没有 BANK${BANK}_APP_ADDR"
fi

# ---- 前置检查 ----
[ -f "$MPCLI_EXE" ] || die "找不到 mpcli.exe: $MPCLI_EXE"
[ -f "$FW" ]        || die "找不到镜像: $FW
       先生成: ./tools/scripts/build.sh --bank $BANK"

# 下面会 cd 到 mpcli.exe 所在目录（WindowsInterop 路径），-F 必须是绝对路径。
# wslpath 对相对路径只做格式转换，不会补全。
FW="$(cd "$(dirname "$FW")" && pwd)/$(basename "$FW")"

echo "[app-flash] exe  = $MPCLI_EXE"
echo "[app-flash] port = $PORT   addr = $ADDR   baud = $BAUD"
echo "[app-flash] fw   = $FW"

# ---- WSLInterop 检测（同 scripts/flash-wsl-app.sh）----
# 未注册时直接执行 .exe 会报 "Exec format error"，改用 /init + 临时 .bat。
interop_ok=0
if [ -r /proc/sys/fs/binfmt_misc/WSLInterop ]; then
    grep -q '^enabled' /proc/sys/fs/binfmt_misc/WSLInterop && interop_ok=1
fi

if command -v wslpath >/dev/null 2>&1; then
    FW_WIN="$(wslpath -w "$FW")"
else
    FW_WIN="$FW"
fi

if [ "$interop_ok" = 1 ]; then
    # mpcli.exe(frozen) 用 sys.executable 目录定位 fw/、config/，与 CWD 无关；
    # 这里 cd 到 exe 目录纯粹是和其他 flash 脚本保持一致。
    cd "$(dirname "$MPCLI_EXE")"
    set -- -c "$PORT" -T RTL87X3G -M 5 -p -A "$ADDR" -F "$FW_WIN" -b "$BAUD" -r -u -d
    if [ "$DRY" = "1" ]; then
        printf '[app-flash][dry-run] %q ' "$MPCLI_EXE"; printf '%q ' "$@"; echo
        exit 0
    fi
    exec "$MPCLI_EXE" "$@"
fi

# ---- WSLInterop 未注册：走 /init + 临时 .bat 回退 ----
echo "[app-flash] 提示: WSLInterop 未注册，改用 /init + 临时 .bat 启动 Windows 进程" >&2
[ -x /init ] || die "找不到 /init；请在 Windows 端直接用 mpcli.exe 烧录"

# .bat 需要放在 Windows 可见的目录；优先用 C:\Windows\Temp 或 %TEMP%。
BAT_DIR=""
for d in /mnt/c/Windows/Temp /mnt/c/Users/*/AppData/Local/Temp; do
    [ -d "$d" ] && [ -w "$d" ] && { BAT_DIR="$d"; break; }
done
[ -n "$BAT_DIR" ] || die "找不到可写的 Windows 临时目录"

TMP_BAT="$(mktemp "$BAT_DIR/mpcli-app-XXXXXX.bat")"
trap 'rm -f "$TMP_BAT"' EXIT INT TERM
# CRT 换行，cmd.exe 才能正确解析。
printf '@echo off\r\n"%s" -c %s -T RTL87X3G -M 5 -p -A %s -F "%s" -b %s -r -u -d\r\nexit /b %%ERRORLEVEL%%\r\n' \
    "$(wslpath -w "$MPCLI_EXE")" "$PORT" "$ADDR" "$FW_WIN" "$BAUD" > "$TMP_BAT"

if [ "$DRY" = "1" ]; then
    echo "[app-flash][dry-run] /init cmd.exe /c \"$(wslpath -w "$TMP_BAT")\""
    echo "[app-flash][dry-run] bat 内容:"; tr -d '\r' < "$TMP_BAT" | sed 's/^/    /'
    exit 0
fi

# cmd.exe 不支持把 UNC 路径（\\wsl.localhost\...）当当前目录，先切到 Windows
# 本地目录，避免启动时的 UNC 警告。这里不用 exec，好让 EXIT trap 删除临时 .bat。
cd "$BAT_DIR"
/init /mnt/c/Windows/System32/cmd.exe /c "$(wslpath -w "$TMP_BAT")"
rc=$?
rm -f "$TMP_BAT"
trap - EXIT INT TERM
exit $rc
