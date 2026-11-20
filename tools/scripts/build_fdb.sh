#!/usr/bin/env bash
# Build the prebuilt FlashDB image
# (tools/pack/fdb_resource_bin/fdb_prebuilt.bin).
#
# Source assets default to tools/pack/fdb_resource_bin (tracked with the repo);
# if that directory has no wp-1.bin, fall back to the designer build tree.
# Groups are explicit because tb1 needs four slots and --assets-dir would skip
# wp-tear.bin and the .avi by default.
#
# Usage:
#   ./tools/scripts/build_fdb.sh
#   ./tools/scripts/build_fdb.sh --assets-dir /path/to/assets/images
#   ./tools/scripts/build_fdb.sh --out /tmp/fdb.bin --no-video
#   ASSETS_DIR=/path/to/images ./tools/scripts/build_fdb.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"                 # 应用工程根目录 = 仓库根

# 素材目录按优先级取：
#   1) 命令行 --assets-dir
#   2) 环境变量 ASSETS_DIR
#   3) 仓库内的 tools/pack/fdb_resource_bin（随工程管理）
#   4) 本 checkout 的 designer 构建树（HoneyGUI Designer 导出的 bin）
#   5) Windows 侧同一工程的 designer 构建树（WSL 内没有导出结果时）
if [ -z "${ASSETS_DIR:-}" ]; then
    for d in "$APP_DIR/tools/pack/fdb_resource_bin" \
             "$APP_DIR/app/designer/build/assets/images" \
             "/mnt/c/wokspace/GitLoadHouse/hmi-manifest_gerrit/Mi-eBadge/zephyrproject/realtek-app/applications/eBadge_8773g_8711f/app/designer/build/assets/images"; do
        [ -f "$d/wp-1.bin" ] && { ASSETS_DIR="$d"; break; }
    done
fi
ASSETS_DIR="${ASSETS_DIR:-}"
OUT="${OUT:-$APP_DIR/tools/pack/fdb_resource_bin/fdb_prebuilt.bin}"
WITH_VIDEO="${WITH_VIDEO:-1}"

die() { echo "[fdb-build] error: $*" >&2; exit 1; }

usage() {
    cat <<EOF
Usage: $(basename "$0") [--assets-dir DIR] [--out FILE] [--no-video]

  --assets-dir DIR   directory with wp-N.bin / pre-N.bin / wp-tear.bin /
                     tear_mask.bin / tear_part.bin / wl-video.avi
                     (default: auto-detect under the designer build tree)
  --out FILE         output image (default: tools/pack/fdb_resource_bin/fdb_prebuilt.bin)
  --no-video         skip wl-video.avi

Environment: ASSETS_DIR, OUT, WITH_VIDEO=0/1
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --assets-dir) ASSETS_DIR="$2"; shift 2 ;;
        --out)        OUT="$2";        shift 2 ;;
        --no-video)   WITH_VIDEO=0;    shift ;;
        -h|--help)    usage; exit 0 ;;
        *) die "unknown argument: $1 (use --help)" ;;
    esac
done

[ -d "$ASSETS_DIR" ] || die "assets directory not found: $ASSETS_DIR"

required="wp-1.bin pre-1.bin wp-2.bin pre-2.bin wp-3.bin pre-3.bin
          wp-tear.bin tear_mask.bin tear_part.bin"
for f in $required; do
    [ -f "$ASSETS_DIR/$f" ] || die "missing asset: $ASSETS_DIR/$f"
done

args=(
    --out "$OUT"
    --file "wp1=$ASSETS_DIR/wp-1.bin,pre=$ASSETS_DIR/pre-1.bin"
    --file "wp2=$ASSETS_DIR/wp-2.bin,pre=$ASSETS_DIR/pre-2.bin"
    --file "wp3=$ASSETS_DIR/wp-3.bin,pre=$ASSETS_DIR/pre-3.bin"
    --file "tb1=$ASSETS_DIR/wp-tear.bin,pre=$ASSETS_DIR/pre-1.bin,mask=$ASSETS_DIR/tear_mask.bin,part=$ASSETS_DIR/tear_part.bin"
)

VIDEO_INCLUDED=0
if [ "$WITH_VIDEO" = "1" ]; then
    # 视频是可选的：fdb_resource_bin 默认不带 4MB 的 wl-video.avi。
    if [ -f "$ASSETS_DIR/wl-video.avi" ]; then
        args+=(--file "video1=$ASSETS_DIR/wl-video.avi")
        VIDEO_INCLUDED=1
    else
        echo "[fdb-build] 提示: $ASSETS_DIR 下没有 wl-video.avi，跳过视频" >&2
    fi
fi

echo "[fdb-build] assets = $ASSETS_DIR"
echo "[fdb-build] output = $OUT"
echo "[fdb-build] video  = $([ "$VIDEO_INCLUDED" = "1" ] && echo yes || echo no)"

cd "$APP_DIR"
python3 tools/pack/pack.py fdb "${args[@]}"

echo
echo "[fdb-build] done: $OUT ($(stat -c %s "$OUT") B)"
echo "[fdb-build] flash with: ./tools/scripts/flash_fdb.sh"
