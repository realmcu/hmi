#!/usr/bin/env bash
#
# build.sh —— 调用 west build 编译 RTL87X3G 应用。
#
# 对齐 .vscode/tasks.json：
#   - 普通增量编译：west build -b rtl87x3g_evb
#   - 强制全量重编：west build -b rtl87x3g_evb -p
#
# 用法：
#   scripts/build.sh           # 增量编译（默认）
#   scripts/build.sh -p        # 全量重编（pristine）
#   BOARD=rtl87x3g_evb scripts/build.sh
#   DRY=1 scripts/build.sh     # 只打印将要执行的命令，不真正编译
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

BOARD="${BOARD:-rtl87x3g_evb}"
PRISTINE=""

# 解析参数：-p 或 --pristine 触发全量重编
for arg in "$@"; do
  case "$arg" in
    -p|--pristine) PRISTINE="-p" ;;
    *) echo "[build] 未知参数: $arg" >&2; exit 1 ;;
  esac
done

echo "[build] board = $BOARD${PRISTINE:+   mode = pristine}"

cd "$(realpath "$REPO_ROOT")"

set -- west build -b "$BOARD" $PRISTINE

if [ "${DRY:-0}" = "1" ]; then
  printf '[build][dry-run] '; printf '%q ' "$@"; echo
  exit 0
fi

exec "$@"
