#!/usr/bin/env bash
#
# flash.sh —— 在 WSL2 里驱动 Windows 版 mpcli.exe 烧录 RTL87X3G 的 App 镜像。
#
# 原理：WSL 直接调用 Windows 的 mpcli.exe（原生进程，能访问 COM 口），
#       用 wslpath -w 把 WSL 里的固件路径转成 Windows 形式喂给 -F。
#
# 默认配置对齐 .vscode/tasks.json 里的 "West Flash"：
#   - 工具：v4.0.0.6 Windows 版 mpcli.exe
#   - 固件：bin/app.bin（无 MP 头）
#   - 烧录：-p -A 0x7009E000 -b 3000000 -M 5 -r -u -d -T RTL87X3G
#
# 用法：
#   scripts/flash.sh                 # 默认 COM7
#   scripts/flash.sh COM8            # 指定下载口
#   PORT=COM8 BAUD=2000000 scripts/flash.sh
#   DRY=1 scripts/flash.sh           # 只打印将要执行的命令，不真正烧录
#
# 烧录前请确认设备已进入 MP 下载模式（P2_0 接 GND 后复位）。
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# ---- 可通过位置参数或环境变量覆盖 ----
PORT="${1:-${PORT:-COM7}}"
MPCLI_EXE="${MPCLI_EXE:-/mnt/d/mpcli_meta_tool_v4.0.0.6_win/mpcli.exe}"
FW="${FW:-$REPO_ROOT/bin/app.bin}"
ADDR="${ADDR:-0x7009E000}"
BAUD="${BAUD:-3000000}"

# ---- 检查 ----
[ -f "$MPCLI_EXE" ] || { echo "[flash] mpcli.exe 不存在: $MPCLI_EXE" >&2; exit 1; }
[ -f "$FW" ]        || { echo "[flash] 固件不存在: $FW（先 west build 生成 bin/app.bin）" >&2; exit 1; }

FW_WIN="$(wslpath -w "$FW")"

echo "[flash] exe  = $MPCLI_EXE"
echo "[flash] port = $PORT   addr = $ADDR   baud = $BAUD"
echo "[flash] fw   = $FW"
echo "[flash] fw(win) = $FW_WIN"

# mpcli.exe(frozen) 用 sys.executable 目录定位 fw/、config/，与 CWD 无关；
# 这里 cd 到 exe 目录纯粹是和 tasks.json 的 cwd 保持一致。
cd "$(dirname "$MPCLI_EXE")"

set -- -c "$PORT" -T RTL87X3G -M 5 -p -A "$ADDR" -F "$FW_WIN" -b "$BAUD" -r -u -d

if [ "${DRY:-0}" = "1" ]; then
  printf '[flash][dry-run] %q ' "$MPCLI_EXE"; printf '%q ' "$@"; echo
  exit 0
fi

exec "$MPCLI_EXE" "$@"
