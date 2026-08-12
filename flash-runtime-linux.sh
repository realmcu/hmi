#!/usr/bin/env bash
#
# flash-runtime-linux.sh —— 在原生 Linux 下用 mpcli 烧录随包自带的 runtime 固件。
#
# 与 flash-linux.sh 的区别：
#   - flash-linux.sh 烧的是单个 App 镜像（-p -A 0x7009E000 -F bin/app.bin）。
#   - 本脚本烧的是整套 runtime（boot patch / upperstack / OTA header /
#     stack & sys patch / DSP sys+app+cfg / SYSTEM & APP Config，共 11 个镜像），
#     由 mpcli 的 --flash-runtime 一条命令生成 flash_image.json 后 -a 全刷。
#
# runtime 里【不含 APP】（地址表里没有 0x7009E000），完整刷机顺序：
#   1. scripts/flash-runtime-linux.sh      # 先烧 runtime
#   2. scripts/flash-linux.sh              # 再烧 bin/app.bin
#
# 用法：
#   scripts/flash-runtime-linux.sh                      # 默认 /dev/ttyUSB0
#   scripts/flash-runtime-linux.sh /dev/ttyUSB1         # 指定下载口
#   PORT=/dev/ttyUSB1 BAUD=3000000 scripts/flash-runtime-linux.sh
#   CHIP=RTL8783G scripts/flash-runtime-linux.sh        # 换芯片
#   DRY=1 scripts/flash-runtime-linux.sh                # 只打印将要执行的命令
#
# 备注：访问 /dev/ttyUSB* 需当前用户在 dialout 组：
#   sudo usermod -aG dialout "$USER"   # 加组后需重新登录
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ---- 可通过位置参数或环境变量覆盖 ----
PORT="${1:-${PORT:-/dev/ttyUSB0}}"
MPCLI="${MPCLI:-$SCRIPT_DIR/tool/mpcli/mpcli}"
# CHIP 取 tool/mpcli/fw/runtime_system_bin/ 下的目录名：RTL8773G / RTL8783G / RTL87X3EP
CHIP="${CHIP:-RTL8773G}"
BAUD="${BAUD:-2000000}"

# ---- 检查 ----
[ -x "$MPCLI" ] || { echo "[flash-runtime] mpcli 不存在或不可执行: $MPCLI" >&2; exit 1; }
[ -d "$(dirname "$MPCLI")/fw/runtime_system_bin/$CHIP" ] || {
  echo "[flash-runtime] 芯片目录不存在: fw/runtime_system_bin/$CHIP" >&2
  echo "[flash-runtime] 可选: $(ls "$(dirname "$MPCLI")/fw/runtime_system_bin" | tr '\n' ' ')" >&2
  exit 1
}
[ -e "$PORT" ] || { [ "${DRY:-0}" = "1" ] || { echo "[flash-runtime] 串口不存在: $PORT（检查 USB 连接，或 ls /dev/ttyUSB*）" >&2; exit 1; }; }
[ -w "$PORT" ] || { [ "${DRY:-0}" = "1" ] || { echo "[flash-runtime] 串口不可写: $PORT（把用户加入 dialout 组：sudo usermod -aG dialout \$USER，重新登录后生效）" >&2; exit 1; }; }

echo "[flash-runtime] mpcli = $MPCLI"
echo "[flash-runtime] port  = $PORT   chip = $CHIP   baud = $BAUD"

# mpcli(frozen) 用 sys.executable 目录定位 fw/、config/，与 CWD 无关；
# 这里 cd 到 mpcli 目录纯粹是和 flash-linux.sh 的行为保持一致。
cd "$(dirname "$MPCLI")"

# -u -d 必须显式给：mpcli 的 --flash-runtime 只注入 -M 5 -r -b 3000000，
# 而 SYSTEM_Config 落在受保护的 0x70002000（OEM_CFG 区），少了 -u 写不进去。
# -T 由 CHIP 自动推导（RTL8773G -> RTL87X3G），不用传。
set -- --flash-runtime "$CHIP" -c "$PORT" -u -d -b "$BAUD"

if [ "${DRY:-0}" = "1" ]; then
  printf '[flash-runtime][dry-run] %q ' "$MPCLI"; printf '%q ' "$@"; echo
  exit 0
fi

exec "$MPCLI" "$@"
