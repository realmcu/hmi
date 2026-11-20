#!/usr/bin/env bash
#
# build.sh -- build the RTL87X3G application with west build.
#
# Aligns with .vscode/tasks.json:
#   - Normal incremental build: west build -b rtl87x3g_evb
#   - Force full rebuild: west build -b rtl87x3g_evb -p
#
# Usage:
#   scripts/build.sh           # incremental build (default)
#   scripts/build.sh -p        # full rebuild (pristine)
#   BOARD=rtl87x3g_evb scripts/build.sh
#   DRY=1 scripts/build.sh     # print the commands only, do not actually build
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

BOARD="${BOARD:-rtl87x3g_evb}"
PRISTINE=""

# Parse arguments: -p or --pristine triggers a full rebuild
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
