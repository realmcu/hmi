#!/usr/bin/env bash
# Flash only the app image (do not change romfs / FDB) — drives the Windows mpcli.exe from WSL2.
#
# Based on scripts/flash-wsl-app.sh (separate repo HoneyRepo/hmi), adapted to this project's layout:
#   - repo root is derived from tools/scripts/, so it can be run from any directory
#   - default image and address are derived from --bank; the address comes from flash_map.ini, not hardcoded
#   - default image is the OTA artifact bin/bank<N>/app_bank<N>.bin;
#     for a packed full image (app+romfs) use --fw bin/bank<N>/app_bank<N>_packed.bin
#
# How it works: WSL calls the Windows mpcli.exe directly (a native process that can access COM ports),
#       and wslpath -w converts the WSL firmware path to Windows form for -F.
#
# Usage:
#   ./tools/scripts/flash_app.sh                       # default bank0 + COM5
#   ./tools/scripts/flash_app.sh COM7                  # positional argument = serial port
#   ./tools/scripts/flash_app.sh --bank 1
#   ./tools/scripts/flash_app.sh --fw bin/bank0/app_bank0_packed.bin
#   DRY=1 ./tools/scripts/flash_app.sh                 # print the command only
#
# Environment variables: MPCLI_EXE FW ADDR PORT BAUD DRY=1
#   MPCLI_EXE defaults to scripts/tool/mpcli/mpcli.exe (same as flash_fdb.sh / flash_romfs.sh)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"                 # application project root = repo root

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
        *)  PORT="$1"; shift ;;      # positional argument = port
    esac
done

case "$BANK" in
    0|1) ;;
    *) echo "错误: --bank 只能是 0 或 1" >&2; exit 1 ;;
esac

die() { echo "[app-flash] 错误: $*" >&2; exit 1; }

FW="${FW:-$APP_DIR/bin/bank${BANK}/app_bank${BANK}.bin}"

# ---- flash address: read from flash_map.ini by default, so the script does not duplicate a constant ----
if [ -z "$ADDR" ]; then
    [ -f "$FLASH_MAP" ] || die "找不到 flash_map.ini: $FLASH_MAP"
    ADDR=$(grep -E "^BANK${BANK}_APP_ADDR=" "$FLASH_MAP" | cut -d= -f2 | tr -d '[:space:]' || true)
    [ -n "$ADDR" ] || die "flash_map.ini 里没有 BANK${BANK}_APP_ADDR"
fi

# ---- preflight checks ----
[ -f "$MPCLI_EXE" ] || die "找不到 mpcli.exe: $MPCLI_EXE"
[ -f "$FW" ]        || die "找不到镜像: $FW
       先生成: ./tools/scripts/build.sh --bank $BANK"

# Below we cd into mpcli.exe's directory (a WindowsInterop path), so -F must be an absolute path.
# wslpath only reformats relative paths; it does not resolve them.
FW="$(cd "$(dirname "$FW")" && pwd)/$(basename "$FW")"

echo "[app-flash] exe  = $MPCLI_EXE"
echo "[app-flash] port = $PORT   addr = $ADDR   baud = $BAUD"
echo "[app-flash] fw   = $FW"

# ---- WSLInterop detection (same as scripts/flash-wsl-app.sh) ----
# When it is not registered, running the .exe directly reports "Exec format error"; use /init + a temp .bat instead.
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
    # mpcli.exe (frozen) locates fw/ and config/ from the sys.executable directory, regardless of CWD;
    # we cd to the exe directory here purely to stay consistent with the other flash scripts.
    cd "$(dirname "$MPCLI_EXE")"
    set -- -c "$PORT" -T RTL87X3G -M 5 -p -A "$ADDR" -F "$FW_WIN" -b "$BAUD" -r -u -d
    if [ "$DRY" = "1" ]; then
        printf '[app-flash][dry-run] %q ' "$MPCLI_EXE"; printf '%q ' "$@"; echo
        exit 0
    fi
    exec "$MPCLI_EXE" "$@"
fi

# ---- WSLInterop not registered: /init + temp .bat fallback ----
echo "[app-flash] 提示: WSLInterop 未注册，改用 /init + 临时 .bat 启动 Windows 进程" >&2
[ -x /init ] || die "找不到 /init；请在 Windows 端直接用 mpcli.exe 烧录"

# The .bat must live in a directory Windows can see; prefer C:\Windows\Temp or %TEMP%.
BAT_DIR=""
for d in /mnt/c/Windows/Temp /mnt/c/Users/*/AppData/Local/Temp; do
    [ -d "$d" ] && [ -w "$d" ] && { BAT_DIR="$d"; break; }
done
[ -n "$BAT_DIR" ] || die "找不到可写的 Windows 临时目录"

TMP_BAT="$(mktemp "$BAT_DIR/mpcli-app-XXXXXX.bat")"
trap 'rm -f "$TMP_BAT"' EXIT INT TERM
# CRLF line endings so cmd.exe parses it correctly.
printf '@echo off\r\n"%s" -c %s -T RTL87X3G -M 5 -p -A %s -F "%s" -b %s -r -u -d\r\nexit /b %%ERRORLEVEL%%\r\n' \
    "$(wslpath -w "$MPCLI_EXE")" "$PORT" "$ADDR" "$FW_WIN" "$BAUD" > "$TMP_BAT"

if [ "$DRY" = "1" ]; then
    echo "[app-flash][dry-run] /init cmd.exe /c \"$(wslpath -w "$TMP_BAT")\""
    echo "[app-flash][dry-run] bat 内容:"; tr -d '\r' < "$TMP_BAT" | sed 's/^/    /'
    exit 0
fi

# cmd.exe does not accept a UNC path (\\wsl.localhost\...) as the current directory, so switch to a
# Windows-local directory first to avoid the UNC warning on startup. We avoid exec so the EXIT trap can delete the temp .bat.
cd "$BAT_DIR"
/init /mnt/c/Windows/System32/cmd.exe /c "$(wslpath -w "$TMP_BAT")"
rc=$?
rm -f "$TMP_BAT"
trap - EXIT INT TERM
exit $rc
