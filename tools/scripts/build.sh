#!/usr/bin/env bash
# eBadge one-shot cold build (+ optional flashing), WSL/Linux version, counterpart of build.ps1 (Windows version).
#
# The script lives in tools/scripts/, but all relative paths (build_*, bin/, snippets/) are relative
# to the repo root, so it can be run from any directory.
#
# Default arguments = --bank 0 --snippet wifi (the usual combination on this machine: OTA bank0 + external 8711).
# Running ./tools/scripts/build.sh with no arguments is enough. To return to the old behavior (single bank, no snippet):
#   ./tools/scripts/build.sh --bank '' --snippet ''
#
# -Bank is for OTA dual-bank builds (see sample/ota):
#   '0'  adds --snippet flash_32M_bank0, separate -d build_bank0, output bin/bank0/app_bank0.bin (default).
#   '1'  adds --snippet flash_32M_bank1, output bin/bank1/app_bank1.bin.
#   'both'   builds bank0 then bank1 in order (if one fails the next is not built, same as the ps1 version).
#   ''   adds no snippet, output bin/app.bin (old behavior, for ordinary development builds before OTA).
# bank app address: read at run time from BANK<N>_APP_ADDR in flash_map.ini (currently bank0=0x7009E000,
# bank1=0x704F3000); it is not hardcoded in this script.
#
# --snippet passes any additional snippet (repeatable; wifi is included by default) and stacks with --bank. This project ships:
#   wifi            external 8711 Wi-Fi. This device acts as SPI host @25MHz with the Realtek standard AT command set.
#   flash_32M_bank0/1  32M flash partition table (OTA bank0 / bank1).
#
# flash_32M_bank* lives under this project's snippets/ (tracked in this repo) and does not use realtek-app/snippets.
# Two snippets with the same name do not override each other, they each append an overlay, so before building we check that the resolved one is this project's.
#
# Examples:
#   ./tools/scripts/build.sh                          # default: bank0 + wifi (incremental)
#   ./tools/scripts/build.sh --pristine               # first build / clean rebuild after adding .c files
#   ./tools/scripts/build.sh --bank both --pristine   # clean rebuild of bank0 and bank1 in order
set -euo pipefail

# ===== paths (change here on another machine, or override with environment variables) =====
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"                 # application project root = repo root
REALTEK_APP_DIR="$(cd "$APP_DIR/../.." && pwd)"            # realtek-app/

VENV_PYTHON="${VENV_PYTHON:-/home/sheldeng/Mi_eBadge/.venv/bin/python}"   # west runs in this venv (zephyr dependencies installed)
SDK_DIR="${SDK_DIR:-/home/sheldeng/zephyr-sdk-0.16.9}"                     # Zephyr SDK (setup.sh -c already run to register the cmake package)
BOARD="${BOARD:-rtl87x3g_evb}"
PORT_DEFAULT="${PORT_DEFAULT:-/dev/ttyUSB0}"                # under WSL2 the USB-to-serial device must first be attached with usbipd-win
MPCLI="${MPCLI:-$REALTEK_APP_DIR/tools/mpcli/Linux/mpcli}"

usage() {
    cat <<EOF
用法: $(basename "$0") [--pristine] [--flash] [--port <dev>] [--bank ''|0|1|both]
                     [--snippet <name>]... [--pack [--no-mp]] [--romfs <file>]
                      [--romfs-offset <hex>]

环境变量: VENV_PYTHON SDK_DIR BOARD PORT_DEFAULT MPCLI
EOF
}

PRISTINE=0
FLASH=0
PORT="$PORT_DEFAULT"
BANK="0"
SNIPPETS=()
SNIPPETS_EXPLICIT=0
PACK=0
PACK_MP=1
ROMFS_BIN="$APP_DIR/app/designer/build/app_romfs_0x70536400.bin"
PYTHON_PACK="$APP_DIR/tools/pack/pack.py"
ROMFS_OFFSET_ARG=""

# Newer Designer versions put user source files in src/user/, and romfs_layout.h moved with them.
# Both locations may exist; the copy in src/ shadows the one in src/user/, so read whichever is actually in effect.
resolve_layout_h() {
    local a="$APP_DIR/app/designer/src/romfs_layout.h"
    local b="$APP_DIR/app/designer/src/user/romfs_layout.h"
    if [ -f "$a" ]; then echo "$a"
    elif [ -f "$b" ]; then echo "$b"
    elif [ -d "$(dirname "$b")" ]; then echo "$b"
    else echo "$a"; fi
}

# pack.py fwcheck returns 2 = the romfs mount address in the ELF is still the old one.
# Any other return code (0 / unable to check) is treated as OK, so a missing environment does not block packing.
#
# --elf is passed explicitly: build_bank<N>_wifi and build_bank<N> can both exist, and letting pack.py
# pick by itself easily picks the one not in use, so it always reports stale and rebuilds every round until the rounds run out.
fw_mount_ok() { # $1=bank_sel $2=offset
    local rc=0
    "$VENV_PYTHON" "$PYTHON_PACK" fwcheck --bank "bank$1" --offset "$2" \
        --elf "$APP_DIR/build_bank$1$snip_suffix/zephyr/zephyr.elf" \
        >/dev/null 2>&1 || rc=$?
    [ "$rc" -ne 2 ]
}

while [ $# -gt 0 ]; do
    case "$1" in
        --pristine) PRISTINE=1; shift ;;
        --flash) FLASH=1; shift ;;
        --port) PORT="$2"; shift 2 ;;
        --bank) BANK="$2"; shift 2 ;;
        --pack) PACK=1; shift ;;
        --no-mp) PACK_MP=0; shift ;;
        --romfs) ROMFS_BIN="$2"; shift 2 ;;
        --romfs-offset) ROMFS_OFFSET_ARG="$2"; shift 2 ;;
        --snippet)
            # --snippet was passed explicitly (an empty value means "no default wifi"); wifi is not added automatically afterwards
            SNIPPETS_EXPLICIT=1
            if [ -n "$2" ]; then SNIPPETS+=("$2"); fi
            shift 2
            ;;
        -h|--help) usage; exit 0 ;;
        *) echo "未知参数: $1" >&2; usage; exit 1 ;;
    esac
done

case "$BANK" in
    ''|0|1|both) ;;
    *) echo "错误: --bank 只能是 '' / 0 / 1 / both" >&2; exit 1 ;;
esac

# default snippet = wifi; any explicit --snippet (including an empty value) disables the automatic addition
if [ "$SNIPPETS_EXPLICIT" -eq 0 ] && [ ${#SNIPPETS[@]} -eq 0 ]; then
    SNIPPETS=(wifi)
fi

# ===== preflight checks =====
[ -x "$VENV_PYTHON" ] || { echo "找不到 venv python: $VENV_PYTHON" >&2; exit 1; }
[ -x "$SDK_DIR/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc" ] || { echo "找不到 Zephyr SDK 工具链: $SDK_DIR" >&2; exit 1; }
for b in 0 1; do
    [ -f "$APP_DIR/snippets/flash_32M_bank$b/snippet.yml" ] || {
        echo "找不到本工程的分区表 snippet: snippets/flash_32M_bank$b/snippet.yml" >&2
        exit 1
    }
done

# ===== environment variables =====
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
export ZEPHYR_SDK_INSTALL_DIR="$SDK_DIR"
echo "[env]   VARIANT=zephyr  SDK=$SDK_DIR"

snip_suffix=""
if [ ${#SNIPPETS[@]} -gt 0 ]; then
    snip_suffix="_$(IFS=_; echo "${SNIPPETS[*]}")"
    echo "[snip]  ${SNIPPETS[*]}"
fi

# ===== timing (bash has no Stopwatch; the SECONDS builtin is good enough) =====
TIMING_ORDER=()
declare -A TIMING_SEC
declare -A TIMING_OK

measure_phase() {
    local name="$1"; shift
    local start=$SECONDS
    local ok=0
    if "$@"; then ok=1; fi
    local elapsed=$((SECONDS - start))
    TIMING_ORDER+=("$name")
    TIMING_SEC["$name"]=$elapsed
    TIMING_OK["$name"]=$ok
    if [ "$ok" -eq 1 ]; then
        echo "[time]  $name OK ${elapsed}s"
        return 0
    else
        echo "[time]  $name 失败 ${elapsed}s"
        return 1
    fi
}

print_summary() {
    [ ${#TIMING_ORDER[@]} -eq 0 ] && return
    local mode="增量"
    [ "$PRISTINE" -eq 1 ] && mode="pristine 干净重建"
    echo
    echo "===== 耗时（$mode）====="
    for name in "${TIMING_ORDER[@]}"; do
        if [ "${TIMING_OK[$name]}" -eq 1 ]; then
            printf "  %-24s %8ss\n" "$name" "${TIMING_SEC[$name]}"
        else
            printf "  %-24s %8ss   <-- 失败\n" "$name" "${TIMING_SEC[$name]}"
        fi
    done
    if [ ${#TIMING_ORDER[@]} -gt 1 ]; then
        printf "  %-24s %8ss\n" "总计" "$((SECONDS - TOTAL_START))"
    fi
}

# ===== one build (+ optional flashing). $1: ''=default, '0', '1' =====
invoke_app_build() {
    local bank_sel="$1"
    local build_args=(-m west build -b "$BOARD")
    if [ -n "$bank_sel" ]; then
        build_args+=(-d "build_bank${bank_sel}${snip_suffix}" --snippet "flash_32M_bank${bank_sel}")
    elif [ -n "$snip_suffix" ]; then
        build_args+=(-d "build${snip_suffix}")
    fi
    for s in "${SNIPPETS[@]}"; do
        build_args+=(--snippet "$s")
    done
    [ "$PRISTINE" -eq 1 ] && build_args+=(-p always)

    echo "[build] (bank='$bank_sel') $VENV_PYTHON ${build_args[*]}"
    local label="build"
    [ -n "$bank_sel" ] && label="build bank${bank_sel}"
    measure_phase "$label" "$VENV_PYTHON" "${build_args[@]}" || return 1

    # Check that the partition table really comes from this project: for same-named snippets Zephyr appends
    # each overlay separately (the latter does not override the former), so if realtek-app/snippets has one too, both take effect.
    if [ -n "$bank_sel" ]; then
        local want="$APP_DIR/snippets/flash_32M_bank${bank_sel}/flash_32M_bank${bank_sel}.overlay"
        local gen="$APP_DIR/build_bank${bank_sel}${snip_suffix}/zephyr/snippets_generated.cmake"
        local found
        found=$(grep -c "EXTRA_DTC_OVERLAY_FILE.*flash_32M_bank${bank_sel}\.overlay" "$gen" 2>/dev/null || true)
        if [ "$found" != "1" ]; then
            echo "[snip]  错误: flash_32M_bank${bank_sel} 解析到 $found 份 overlay（应为 1）" >&2
            echo "[snip]  realtek-app/snippets 下若也有同名目录，请删除它，否则分区表会被叠加两次" >&2
            return 1
        fi
        grep -q "$want" "$gen" || {
            echo "[snip]  错误: 分区表不是取自本工程: $want" >&2
            return 1
        }
        echo "[snip]  分区表 OK <- $want"
    fi

    local bin addr
    if [ -z "$bank_sel" ]; then
        bin="$APP_DIR/bin/app.bin"
    else
        bin="$APP_DIR/bin/bank${bank_sel}/app_bank${bank_sel}.bin"
    fi
    [ -f "$bin" ] || { echo "编译声称成功但找不到镜像: $bin" >&2; return 1; }

    # The flash address comes from flash_map.ini (a plain build with no bank is treated as bank0)
    addr=$(grep -E "^BANK${bank_sel:-0}_APP_ADDR=" "$APP_DIR/flash_map.ini" \
           | cut -d= -f2 | tr -d '[:space:]' || true)
    [ -n "$addr" ] || {
        echo "flash_map.ini 里没有 BANK${bank_sel:-0}_APP_ADDR" >&2
        return 1
    }
    echo "[build] OK -> $bin (@ $addr)"

    # ===== optional: merge romfs into the app image =====
    #
    # The in-image romfs offset ROMFS_OFFSET is written to app/designer/src[/user]/romfs_layout.h,
    # and it in turn depends on the size of app — so "build first to learn the offset, the offset is needed to pack,
    # and only a packed image makes the firmware correct" is a cycle. We iterate to convergence: pack -> rebuild if the offset changed -> pack again.
    # A measured incremental rebuild takes ~15s and it usually converges in 2 rounds.
    if [ "$PACK" -eq 1 ]; then
        [ -f "$ROMFS_BIN" ] || { echo "找不到 romfs: $ROMFS_BIN" >&2; return 1; }
        [ -n "$bank_sel" ] || { echo "--pack 需要显式 --bank 0|1|both" >&2; return 1; }

        local layout_h
        layout_h=$(resolve_layout_h)
        local merged="$APP_DIR/bin/bank${bank_sel}/app_bank${bank_sel}_packed.bin"
        # for standalone flashing: a relocated copy of romfs with a 4K-aligned target address
        local romfs_only="$APP_DIR/bin/bank${bank_sel}/app_romfs_bank${bank_sel}_reloc.bin"
        local round=0
        while :; do
            round=$((round+1))
            local before after
            before=$(grep -oE 'ROMFS_OFFSET 0x[0-9A-Fa-f]+' "$layout_h" 2>/dev/null | awk '{print $2}')
            local pack_mp_args=()
            [ "$PACK_MP" -eq 1 ] && pack_mp_args+=(--mp)

            # --mp: append a 512B MP header. mpcli uses it to identify the image type/version;
            #       the flash address still comes from -A or the json (not from the MP header).
            measure_phase "pack bank${bank_sel} (round $round)" \
                "$VENV_PYTHON" "$PYTHON_PACK" app \
                    --bank "bank${bank_sel}" --app "$bin" --romfs "$ROMFS_BIN" \
                    --out "$merged" --sign "${pack_mp_args[@]}" \
                    --romfs-out "$romfs_only" \
                    ${ROMFS_OFFSET_ARG:+--romfs-offset $ROMFS_OFFSET_ARG}
            after=$(grep -oE 'ROMFS_OFFSET 0x[0-9A-Fa-f]+' "$layout_h" 2>/dev/null | awk '{print $2}')

            # There are two convergence conditions: the offset did not change, and the firmware ELF really mounts this address.
            # The latter is essential: after romfs_layout.h moved (src/ -> src/user/),
            # the old ninja dependency is stale, so Mi_eBadgeEntry.c is not rebuilt even when the offset is unchanged,
            # and the ELF stays stuck at the old mount address — right image, wrong firmware, all-black UI.
            local need_rebuild=0 reason=""
            if [ "$before" != "$after" ]; then
                need_rebuild=1
                reason="ROMFS_OFFSET $before -> $after"
            elif [ -n "$after" ] && ! fw_mount_ok "$bank_sel" "$after"; then
                need_rebuild=1
                reason="固件 ELF 里的 romfs 挂载地址不是 $after"
            fi
            if [ "$need_rebuild" -eq 0 ]; then
                echo "[pack] 收敛于 ROMFS_OFFSET=$after（第 $round 轮）"
                break
            fi
            if [ "$round" -ge 6 ]; then
                echo "[pack] 6 轮仍未收敛（$reason），放弃" >&2
                return 1
            fi
            echo "[pack] $reason，重编 app 后重试"
            # The header may just have moved, so touch the source to make sure Mi_eBadgeEntry.c is really rebuilt.
            touch "$APP_DIR/app/designer/src/Mi_eBadgeEntry.c"
            build_args=(-m west build -b "$BOARD")
            build_args+=(-d "build_bank${bank_sel}${snip_suffix}" --snippet "flash_32M_bank${bank_sel}")
            for s in "${SNIPPETS[@]}"; do build_args+=(--snippet "$s"); done
            measure_phase "rebuild bank${bank_sel} ($round)" "$VENV_PYTHON" "${build_args[@]}" || return 1
        done
        bin="$merged"
        echo "[pack] OK -> $bin"

        local roff raddr
        roff=$(grep -oE 'ROMFS_OFFSET 0x[0-9A-Fa-f]+' "$layout_h" | awk '{print $2}')
        raddr=$(python3 -c "print('0x%08X' % (int('$addr', 0) + $roff))" 2>/dev/null || echo "?")
        echo "[pack] 单独更新壁纸（不烧 app，省时）："
        echo "       ./tools/scripts/flash_romfs.sh --bank $bank_sel"
        echo "       （等价命令: mpcli -c <port> -p -A $raddr -F $romfs_only ...）"
    fi

    # ===== optional flashing =====
    if [ "$FLASH" -eq 1 ]; then
        [ -x "$MPCLI" ] || { echo "找不到 mpcli: $MPCLI" >&2; return 1; }
        [ -e "$PORT" ] || echo "[flash] 警告: 串口 $PORT 不存在（WSL2 下需要先 usbipd attach）" >&2
        local mpcli_dir
        mpcli_dir="$(cd "$(dirname "$MPCLI")" && pwd)"
        echo "[flash] $PORT  <-  $bin  ($addr / RTL87X3G)"
        local flabel="flash"
        [ -n "$bank_sel" ] && flabel="flash bank${bank_sel}"
        measure_phase "$flabel" bash -c "cd '$mpcli_dir' && '$MPCLI' -c '$PORT' -p -A '$addr' -F '$bin' -b 3000000 -M 5 -r -u -d -T RTL87X3G"
        echo "[flash] OK"
    fi
}

# ===== build (restore the original directory afterwards and always print the timing summary) =====
ORIG_DIR="$(pwd)"
TOTAL_START=$SECONDS

on_exit() {
    local rc=$?
    cd "$ORIG_DIR" 2>/dev/null || true
    print_summary
    exit $rc
}
trap on_exit EXIT

cd "$APP_DIR"
case "$BANK" in
    both) invoke_app_build 0; invoke_app_build 1 ;;
    0)    invoke_app_build 0 ;;
    1)    invoke_app_build 1 ;;
    *)    invoke_app_build '' ;;
esac
