#!/usr/bin/env bash
# 烧录预制资源 FDB 镜像（tools/pack/fdb_resource_bin/fdb_prebuilt.bin），
# 开机后由 flashdb_prepare()
# 枚举出 mainface_list。
#
# 烧录地址 = KVDB 起始地址，由 tools/pack/layout.json 的 fdb.kvdb_addr 决定，
# 并与固件 port/flashdb/fal_cfg.h 的一致性由 tools/pack/pack.py check 保证。
#
# WSL 下必须用 Windows 版 mpcli.exe：COM 口是 Windows 设备，Linux 版
# mpcli 在 WSL 里打不开。这里沿用 scripts/flash-wsl-app.sh 的两条路径：
#   1) WSLInterop 已注册 -> 直接执行 .exe
#   2) 未注册           -> /init + 临时 .bat
#
# 用法：
#   ./tools/scripts/flash_fdb.sh                     # 默认 COM5
#   ./tools/scripts/flash_fdb.sh COM7                # 指定端口
#   ./tools/scripts/flash_fdb.sh --fdb bin/xxx.bin   # 指定镜像
#   ./tools/scripts/flash_fdb.sh --split 0x100000    # 单段上限（默认 2MB；0 = 整文件一次烧）
#   DRY=1 ./tools/scripts/flash_fdb.sh               # 只打印命令
#
# mpcli 的 -F 会拒绝大于 CONFIG_FLASH_SIZE_MAX 的单个文件（RTL87X3G 默认
# 只有 2MB），所以镜像按 --split（默认 2MB）切片、按连续地址依次烧录。
#   `CHUNK_SIZE=0x100000 ./tools/scripts/flash_fdb.sh` 与 `--split 0x100000` 等价。
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"                 # 应用工程根目录 = 仓库根

MPCLI_EXE="${MPCLI_EXE:-$APP_DIR/scripts/tool/mpcli/mpcli.exe}"
LAYOUT="${LAYOUT:-$APP_DIR/tools/pack/layout.json}"
FW="${FW:-$APP_DIR/tools/pack/fdb_resource_bin/fdb_prebuilt.bin}"
PORT="${PORT:-COM5}"
BAUD="${BAUD:-3000000}"
ADDR=""                       # 留空则从 layout.json 读
CHUNK_SIZE="${CHUNK_SIZE:-0x200000}"   # 单段上限（默认 2MB）；0 = 整文件一次烧

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
        *)  PORT="$1"; shift ;;      # 位置参数 = 端口
    esac
done

die() { echo "[fdb-flash] 错误: $*" >&2; exit 1; }

# ---- 前置检查 ----
[ -f "$MPCLI_EXE" ] || die "找不到 mpcli.exe: $MPCLI_EXE"
[ -f "$FW" ]        || die "找不到镜像: $FW
       先生成: ./tools/scripts/build_fdb.sh"
[ -f "$LAYOUT" ]    || die "找不到 $LAYOUT"

# ---- 烧录地址：与固件 fal_cfg.h 的一致性由 pack.py check 保证 ----
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

# ---- 镜像与区域大小核对 ----
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
# 镜像可以比整个 FDB 区短（尾部未使用的 0xFF 被截断了）：
#   - 不能短于 KVDB（目录项在里面）
#   - 不能长于 kvdb+bf
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

# 与 flash_map.ini 的其它分区做重叠检查，避免顺手覆盖 app/OTA
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

# ---- WSLInterop 检测（同 scripts/flash-wsl-app.sh）----
interop_ok=0
if [ -r /proc/sys/fs/binfmt_misc/WSLInterop ]; then
    grep -q '^enabled' /proc/sys/fs/binfmt_misc/WSLInterop && interop_ok=1
fi

# ---- 分段烧录 --------------------------------------------------------------
# mpcli 内嵌的 config/chip_configs/RTL87X3G/custom_config.ini 里 flash_size
# 默认只有 0x200000，下载时它会拿单个 -F 文件的大小和这个值比较（`>`），
# 超过就报 "please check the file content size"。所以默认按 2MB 切片、
# 依次烧到连续地址；最终 flash 内容与整段烧录一致。
CHUNK=$((CHUNK_SIZE))
ADDR_INT=$((ADDR))
SIZE=$(stat -c %s "$FW")
# 每段都从 4K 对齐地址开始
if [ "$CHUNK" -gt 0 ] && [ $((CHUNK % 4096)) -ne 0 ]; then
    die "分段大小必须 4K 对齐（当前 $CHUNK）"
fi
# --split 0 或大于镜像：整文件一次烧
if [ "$CHUNK" -le 0 ] || [ "$CHUNK" -gt "$SIZE" ]; then
    CHUNK=$SIZE
fi
N=$(( (SIZE + CHUNK - 1) / CHUNK ))
CHUNK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/fdb-flash-XXXXXX")"
trap 'rm -rf "$CHUNK_DIR"' EXIT INT TERM

echo "[fdb-flash] exe   = $MPCLI_EXE"
echo "[fdb-flash] port  = $PORT   addr = $ADDR   baud = $BAUD"
echo "[fdb-flash] 分段  = $N x $CHUNK B (镜像 $SIZE B)"

# 未注册 WSLInterop 时用的临时目录
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
