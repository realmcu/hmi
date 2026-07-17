#!/usr/bin/env bash
#
# flash.sh —— 在原生 Linux 下用 mpcli 烧录 RTL87X3G 的 App 镜像。
#
# 与 flash-wsl.sh 的区别：
#   - 工具用 Linux 原生 mpcli（ELF 可执行），不再调用 Windows 版 mpcli.exe。
#   - 串口是 /dev/ttyUSB* 而非 COMx，路径也无需 wslpath 转换。
#
# 默认配置对齐 flash-wsl.sh 的烧录参数：
#   - 工具：mpcli v4.0.0.7（~/.local/mpcli/mpcli）
#   - 固件：bin/app.bin（无 MP 头）
#   - 烧录：-p -A 0x7009E000 -b 2000000 -M 5 -r -u -d -T RTL87X3G
#
# 用法：
#   scripts/flash.sh                        # 默认 /dev/ttyUSB0
#   scripts/flash.sh /dev/ttyUSB1           # 指定下载口
#   PORT=/dev/ttyUSB1 BAUD=3000000 scripts/flash.sh
#   DRY=1 scripts/flash.sh                  # 只打印将要执行的命令，不真正烧录
#
# 备注：访问 /dev/ttyUSB* 需当前用户在 dialout 组：
#   sudo usermod -aG dialout "$USER"   # 加组后需重新登录
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# ---- 可通过位置参数或环境变量覆盖 ----
PORT="${1:-${PORT:-/dev/ttyUSB0}}"
MPCLI="${MPCLI:-$HOME/.local/mpcli/mpcli}"
FW="${FW:-$REPO_ROOT/bin/app.bin}"
ADDR="${ADDR:-0x7009E000}"
BAUD="${BAUD:-2000000}"

# ---- 检查 ----
[ -x "$MPCLI" ] || { echo "[flash] mpcli 不存在或不可执行: $MPCLI" >&2; exit 1; }
[ -f "$FW" ]    || { echo "[flash] 固件不存在: $FW（先 west build 生成 bin/app.bin）" >&2; exit 1; }
[ -e "$PORT" ]  || { echo "[flash] 串口不存在: $PORT（检查 USB 连接，或 ls /dev/ttyUSB*）" >&2; exit 1; }
[ -w "$PORT" ]  || { echo "[flash] 串口不可写: $PORT（把用户加入 dialout 组：sudo usermod -aG dialout \$USER，重新登录后生效）" >&2; exit 1; }

echo "[flash] mpcli = $MPCLI"
echo "[flash] port  = $PORT   addr = $ADDR   baud = $BAUD"
echo "[flash] fw    = $FW"

# mpcli(frozen) 用 sys.executable 目录定位 fw/、config/，与 CWD 无关；
# 这里 cd 到 mpcli 目录纯粹是和 flash-wsl.sh 的行为保持一致。
cd "$(dirname "$MPCLI")"

set -- -c "$PORT" -T RTL87X3G -M 5 -p -A "$ADDR" -F "$FW" -b "$BAUD" -r -u -d

if [ "${DRY:-0}" = "1" ]; then
  printf '[flash][dry-run] %q ' "$MPCLI"; printf '%q ' "$@"; echo
  exit 0
fi

exec "$MPCLI" "$@"
