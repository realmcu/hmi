#!/usr/bin/env bash
#
# flash-runtime-wsl.sh —— 在 WSL2 里驱动 Windows 版 mpcli.exe 烧录随包自带的 runtime 固件。
#
# 与 flash-wsl.sh 的区别：
#   - flash-wsl.sh 烧的是单个 App 镜像（-p -A 0x7009E000 -F bin/app.bin）。
#   - 本脚本烧的是整套 runtime（boot patch / upperstack / OTA header /
#     stack & sys patch / DSP sys+app+cfg / SYSTEM & APP Config，共 11 个镜像），
#     由 mpcli 的 --flash-runtime 一条命令生成 flash_image.json 后 -a 全刷。
#
# runtime 里【不含 APP】（地址表里没有 0x7009E000），完整刷机顺序：
#   1. scripts/flash-runtime-wsl.sh COM13   # 先烧 runtime
#   2. scripts/flash-wsl.sh COM13           # 再烧 bin/app.bin
#
# 用法：
#   scripts/flash-runtime-wsl.sh                  # 默认 COM13
#   scripts/flash-runtime-wsl.sh COM8             # 指定 COM 口
#   PORT=COM8 BAUD=3000000 scripts/flash-runtime-wsl.sh
#   CHIP=RTL8783G scripts/flash-runtime-wsl.sh    # 换芯片
#   DRY=1 scripts/flash-runtime-wsl.sh COM8       # 只打印将要执行的命令
#
# 备注：固件目录不需要 wslpath 转换 —— mpcli.exe 用 sys.executable 自己定位 fw/，
#       会自动解析成 \\wsl.localhost\... UNC 路径。代价是走 UNC 比原生 Linux 慢
#       （扫描 11 个 bin 约 2.5s vs 0.03s）；嫌慢可把 fw/runtime_system_bin/<CHIP>
#       拷到 Windows 盘，改用 mpcli.exe --gen-json --bin-dir <该目录> 两步烧。
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ---- 可通过位置参数或环境变量覆盖 ----
PORT="${1:-${PORT:-COM13}}"
MPCLI_EXE="${MPCLI_EXE:-$SCRIPT_DIR/tool/mpcli/mpcli.exe}"
# CHIP 取 tool/mpcli/fw/runtime_system_bin/ 下的目录名：RTL8773G / RTL8783G / RTL87X3EP
CHIP="${CHIP:-RTL8773G}"
BAUD="${BAUD:-2000000}"

# ---- 检查 ----
[ -f "$MPCLI_EXE" ] || { echo "[flash-runtime] mpcli.exe 不存在: $MPCLI_EXE" >&2; exit 1; }
[ -d "$(dirname "$MPCLI_EXE")/fw/runtime_system_bin/$CHIP" ] || {
  echo "[flash-runtime] 芯片目录不存在: fw/runtime_system_bin/$CHIP" >&2
  echo "[flash-runtime] 可选: $(ls "$(dirname "$MPCLI_EXE")/fw/runtime_system_bin" | tr '\n' ' ')" >&2
  exit 1
}

echo "[flash-runtime] exe  = $MPCLI_EXE"
echo "[flash-runtime] port = $PORT   chip = $CHIP   baud = $BAUD"

# mpcli.exe(frozen) 用 sys.executable 目录定位 fw/、config/，与 CWD 无关；
# 这里 cd 到 exe 目录纯粹是和 flash-wsl.sh 的行为保持一致。
cd "$(dirname "$MPCLI_EXE")"

# -u -d 必须显式给：mpcli 的 --flash-runtime 只注入 -M 5 -r -b 3000000，
# 而 SYSTEM_Config 落在受保护的 0x70002000（OEM_CFG 区），少了 -u 写不进去。
# -T 由 CHIP 自动推导（RTL8773G -> RTL87X3G），不用传。
set -- --flash-runtime "$CHIP" -c "$PORT" -u -d -b "$BAUD"

if [ "${DRY:-0}" = "1" ]; then
  printf '[flash-runtime][dry-run] %q ' "$MPCLI_EXE"; printf '%q ' "$@"; echo
  exit 0
fi

exec "$MPCLI_EXE" "$@"
