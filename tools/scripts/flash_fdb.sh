#!/usr/bin/env bash
# Flash the prebuilt resource FDB image (tools/pack/fdb_resource_bin/fdb_prebuilt.bin);
# after boot it is enumerated by flashdb_prepare()
# into mainface_list.
#
# Flash address = KVDB start address, determined by fdb.kvdb_addr in tools/pack/layout.json;
# its consistency with the firmware's port/flashdb/fal_cfg.h is guaranteed by tools/pack/pack.py check.
#
# Under WSL the Windows mpcli.exe must be used: COM ports are Windows devices, so the
# Linux mpcli cannot open them from WSL. This follows the two paths of scripts/flash-wsl-app.sh:
#   1) WSLInterop registered -> run the .exe directly
#   2) not registered        -> /init + temp .bat
#
# Usage:
#   ./tools/scripts/flash_fdb.sh                     # default COM5
#   ./tools/scripts/flash_fdb.sh COM7                # specify the port
#   ./tools/scripts/flash_fdb.sh --fdb bin/xxx.bin   # specify the image
#   ./tools/scripts/flash_fdb.sh --split 0x100000    # per-chunk limit (default 2MB; 0 = flash the whole file at once)
#   DRY=1 ./tools/scripts/flash_fdb.sh               # print the command only
#
# mpcli's -F rejects any single file larger than CONFIG_FLASH_SIZE_MAX (RTL87X3G defaults
# to only 2MB), so the image is sliced by --split (2MB by default) and flashed at consecutive addresses.
#   `CHUNK_SIZE=0x100000 ./tools/scripts/flash_fdb.sh` is equivalent to `--split 0x100000`.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"                 # application project root = repo root

MPCLI_EXE="${MPCLI_EXE:-$APP_DIR/scripts/tool/mpcli/mpcli.exe}"
LAYOUT="${LAYOUT:-$APP_DIR/tools/pack/layout.json}"
FW="${FW:-$APP_DIR/tools/pack/fdb_resource_bin/fdb_prebuilt.bin}"
PORT="${PORT:-COM5}"
BAUD="${BAUD:-3000000}"
ADDR=""                       # empty = read from layout.json
CHUNK_SIZE="${CHUNK_SIZE:-0x200000}"   # per-chunk limit (2MB by default); 0 = flash the whole file at once

usage() {
    cat <<EOF
用法: $(basename "$0") [--port COM<n>] [--fdb FILE] [--addr 0x...] [--baud <n>]
                       [--split <bytes>]

  --fdb FILE      要烧的 FDB 镜像
                  （默认 tools/pack/fdb_resource_bin/fdb_prebuilt.bin）
  --addr 0x...    烧录地址（默认读 tools/pack/layout.json 的 fdb.kvdb_addr）
  --split BYTES   单段上限，默认 0x200000（2MB），0 = 整文件一次烧
  位置参数        COM 口（默认 COM5）

环境变量: MPCLI_EXE LAYOUT FW PORT BAUD CHUNK_SIZE DRY=1
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --port) PORT="$2"; shift 2 ;;
        --fdb)  FW="$2"; shift 2 ;;
        --addr) ADDR="$2"; shift 2 ;;
        --baud) BAUD="$2"; shift 2 ;;
        --split) CHUNK_SIZE=$(( $2 )); shift 2 ;;
        -h|--help) usage; exit 0 ;;
        -*) echo "未知参数: $1" >&2; usage; exit 1 ;;
        *)  PORT="$1"; shift ;;      # positional argument = port
    esac
done

die() { echo "[fdb-flash] 错误: $*" >&2; exit 1; }

# ---- preflight checks ----
[ -f "$MPCLI_EXE" ] || die "找不到 mpcli.exe: $MPCLI_EXE"
[ -f "$FW" ]        || die "找不到镜像: $FW
       先生成: ./tools/scripts/build_fdb.sh"
[ -f "$LAYOUT" ]    || die "找不到 $LAYOUT"

# ---- flash address: consistency with the firmware's fal_cfg.h is guaranteed by pack.py check ----
if [ -z "$ADDR" ]; then
    ADDR=$(python3 - "$LAYOUT" <<'PY'
import json, sys
d = json.load(open(sys.argv[1], encoding="utf-8"))["fdb"]
kv, kv_sz, bf = int(d["kvdb_addr"], 16), int(d["kvdb_size"], 16), int(d["bf_data_addr"], 16)
if bf != kv + kv_sz:
    sys.exit("layout.json: bf_data_addr 必须紧接 kvdb")
print(d["kvdb_addr"])
PY
    ) || die "无法从 layout.json 解析 kvdb_addr"
fi

# ---- image and region size checks ----
python3 - "$FW" "$LAYOUT" "$ADDR" <<'PY'
import json, os, sys
fw, layout, addr = sys.argv[1], sys.argv[2], int(sys.argv[3], 16)
d = json.load(open(layout, encoding="utf-8"))["fdb"]
kv, kv_sz = int(d["kvdb_addr"], 16), int(d["kvdb_size"], 16)
bf_sz = int(d["bf_data_size"], 16)
size = os.path.getsize(fw)
total = kv_sz + bf_sz

if addr != kv:
    sys.exit("[fdb-flash] 错误: 烧录地址 %#x 与 kvdb_addr %#x 不一致" % (addr, kv))
# The image may be shorter than the whole FDB region (trailing unused 0xFF is trimmed):
#   - it must not be shorter than the KVDB (the directory entries live there)
#   - it must not be longer than kvdb+bf
if size < kv_sz:
    sys.exit("[fdb-flash] 错误: 镜像 %d B 小于 KVDB (%d B)，目录项不完整" % (size, kv_sz))
if size > total:
    sys.exit("[fdb-flash] 错误: 镜像 %d B 超过 kvdb+bf 区 %d B (%#x)" % (size, total, total))
if size < total:
    print("[fdb-flash] 尾部 %d B 未写入（保持擦除态 0xFF，BF 区未启用部分）"
          % (total - size))
print("[fdb-flash] 镜像   = %s" % fw)
print("[fdb-flash] 大小   = %d B (%.2f MB)" % (size, size / 1048576))
print("[fdb-flash] 区域   = %#x .. %#x  (kvdb %#x +%#x, bf %#x +%#x)"
      % (kv, kv + total, kv, kv_sz, kv + kv_sz, bf_sz))

# Overlap check against the other partitions in flash_map.ini, so app/OTA are not overwritten by accident
ini_path = os.path.join(os.path.dirname(layout), "..", "..", "flash_map.ini")
if os.path.exists(ini_path):
    import re
    reg = {}
    for line in open(ini_path, encoding="utf-8"):
        m = re.match(r"^(\w+)=(0x[0-9A-Fa-f]+)$", line.strip())
        if m:
            reg[m.group(1)] = int(m.group(2), 16)
    lo, hi = kv, kv + total
    bad = []
    for k, v in reg.items():
        if not k.endswith("_ADDR") or v == 0:
            continue
        base = k[:-5]
        sz = reg.get(base + "_SIZE", 0)
        if sz and v < hi and v + sz > lo:
            bad.append("%s %#x..%#x" % (base, v, v + sz))
    if bad:
        sys.exit("[fdb-flash] 错误: 会覆盖其它分区: " + ", ".join(bad))
    print("[fdb-flash] 重叠检查: 通过")
PY

# ---- WSLInterop detection (same as scripts/flash-wsl-app.sh) ----
interop_ok=0
if [ -r /proc/sys/fs/binfmt_misc/WSLInterop ]; then
    grep -q '^enabled' /proc/sys/fs/binfmt_misc/WSLInterop && interop_ok=1
fi

# ---- chunked flashing ------------------------------------------------------
# In the config/chip_configs/RTL87X3G/custom_config.ini embedded in mpcli, flash_size
# defaults to only 0x200000; when downloading it compares the size of a single -F file against it (`>`)
# and reports "please check the file content size" if exceeded. So by default we slice into 2MB
# chunks and flash them at consecutive addresses; the final flash content matches flashing the whole region.
CHUNK=$((CHUNK_SIZE))
ADDR_INT=$((ADDR))
SIZE=$(stat -c %s "$FW")
# Every chunk starts at a 4K-aligned address
if [ "$CHUNK" -gt 0 ] && [ $((CHUNK % 4096)) -ne 0 ]; then
    die "分段大小必须 4K 对齐（当前 $CHUNK）"
fi
# --split 0, or larger than the image: flash the whole file at once
if [ "$CHUNK" -le 0 ] || [ "$CHUNK" -gt "$SIZE" ]; then
    CHUNK=$SIZE
fi
N=$(( (SIZE + CHUNK - 1) / CHUNK ))
CHUNK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/fdb-flash-XXXXXX")"
trap 'rm -rf "$CHUNK_DIR"' EXIT INT TERM

echo "[fdb-flash] exe   = $MPCLI_EXE"
echo "[fdb-flash] port  = $PORT   addr = $ADDR   baud = $BAUD"
echo "[fdb-flash] 分段  = $N x $CHUNK B (镜像 $SIZE B)"

# temp directory used when WSLInterop is not registered
BAT_DIR=""
if [ "$interop_ok" != 1 ]; then
    echo "[fdb-flash] 提示: WSLInterop 未注册，改用 /init + 临时 .bat" >&2
    [ -x /init ] || die "找不到 /init；请在 Windows 端用 mpcli.exe 手动烧录"
    for d in /mnt/c/Windows/Temp /mnt/c/Users/*/AppData/Local/Temp; do
        [ -d "$d" ] && [ -w "$d" ] && { BAT_DIR="$d"; break; }
    done
    [ -n "$BAT_DIR" ] || die "找不到可写的 Windows 临时目录"
fi

run_mpcli() { # <win-file> <addr-hex>
    local fwin="$1" a="$2"
    if [ "$interop_ok" = 1 ]; then
        "$MPCLI_EXE" -c "$PORT" -T RTL87X3G -M 5 -p \
            -A "$a" -F "$fwin" -b "$BAUD" -r -u -d
        return $?
    fi
    local bat rc
    bat="$(mktemp "$BAT_DIR/mpcli-fdb-XXXXXX.bat")"
    printf '@echo off\r\n"%s" -c %s -T RTL87X3G -M 5 -p -A %s -F "%s" -b %s -r -u -d\r\nexit /b %%ERRORLEVEL%%\r\n' \
        "$(wslpath -w "$MPCLI_EXE")" "$PORT" "$a" "$fwin" "$BAUD" > "$bat"
    ( cd "$BAT_DIR" && /init /mnt/c/Windows/System32/cmd.exe /c "$(wslpath -w "$bat")" )
    rc=$?
    rm -f "$bat"
    return $rc
}

i=0
while [ "$i" -lt "$N" ]; do
    off=$((i * CHUNK))
    len=$((SIZE - off))
    [ "$len" -le "$CHUNK" ] || len=$CHUNK
    part="$CHUNK_DIR/part$i.bin"
    dd if="$FW" of="$part" bs=4096 skip="$off" count="$len" \
        iflag=skip_bytes,count_bytes status=none
    part_win="$(wslpath -w "$part")"
    addr_i=$(printf '0x%08x' $((ADDR_INT + off)))
    echo "[fdb-flash] part $((i + 1))/$N: $addr_i +0x$(printf '%x' "$len")"

    if [ "${DRY:-0}" = "1" ]; then
        if [ "$interop_ok" = 1 ]; then
            printf '[fdb-flash][dry-run] %q ' "$MPCLI_EXE"
        else
            printf '[fdb-flash][dry-run] /init cmd.exe /c '
        fi
        printf -- '-c %s -T RTL87X3G -M 5 -p -A %s -F %q -b %s -r -u -d\n' \
            "$PORT" "$addr_i" "$part_win" "$BAUD"
    else
        run_mpcli "$part_win" "$addr_i" || die "第 $((i + 1))/$N 段烧录失败"
    fi
    i=$((i + 1))
done

if [ "${DRY:-0}" = "1" ]; then
    exit 0
fi
echo "[fdb-flash] 完成"
