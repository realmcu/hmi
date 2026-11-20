#!/usr/bin/env bash
# eBadge 冷启动一键编译（+ 可选烧录），WSL/Linux 版，对应 build.ps1（Windows 版）。
#
# 脚本在 tools/scripts/ 下，但所有相对路径（build_*、bin/、snippets/）都相对
# 仓库根目录，从任何目录执行都可以。
#
# 默认参数 = --bank 0 --snippet wifi（本机常用组合：OTA bank0 + 外挂 8711）。
# 不带参数直接 ./tools/scripts/build.sh 即可。要回到老行为（单 bank、无 snippet）：
#   ./tools/scripts/build.sh --bank '' --snippet ''
#
# -Bank 用于 OTA 双 bank 构建（参考 sample/ota）：
#   '0'  加 --snippet flash_32M_bank0，独立 -d build_bank0，编到 bin/bank0/app_bank0.bin（默认）。
#   '1'  加 --snippet flash_32M_bank1，编到 bin/bank1/app_bank1.bin。
#   'both'   依次编 bank0、bank1（其中一个失败就不再编下一个，跟 ps1 版本行为一致）。
#   ''   不加 snippet，编到 bin/app.bin（老行为，OTA 前的普通开发编译用）。
# bank app 地址：运行时读 flash_map.ini 的 BANK<N>_APP_ADDR（当前 bank0=0x7009E000,
# bank1=0x704F3000），不在此脚本里写死。
#
# --snippet 传任意额外 snippet（可多次传，默认已带 wifi），与 --bank 叠加。本工程自带：
#   wifi            外挂 8711 Wi-Fi。本机做 SPI 主机 @25MHz + Realtek 标准 AT 命令集。
#   flash_32M_bank0/1  32M flash 分区表（OTA bank0 / bank1）。
#
# flash_32M_bank* 放在本工程的 snippets/ 下（随本仓库管理），不走 realtek-app/snippets。
# 两处同名不会覆盖而是各追加一次 overlay，所以这里构建前会校验解析到的是本工程那份。
#
# 例：
#   ./tools/scripts/build.sh                          # 默认: bank0 + wifi（增量）
#   ./tools/scripts/build.sh --pristine               # 首次 / 新增 .c 文件后干净重建
#   ./tools/scripts/build.sh --bank both --pristine   # 依次干净重建 bank0 和 bank1
set -euo pipefail

# ===== 路径（换机器改这里，或用环境变量覆盖）=====
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"                 # 应用工程根目录 = 仓库根
REALTEK_APP_DIR="$(cd "$APP_DIR/../.." && pwd)"            # realtek-app/

VENV_PYTHON="${VENV_PYTHON:-/home/sheldeng/Mi_eBadge/.venv/bin/python}"   # west 跑在这个 venv 里（zephyr 依赖已装）
SDK_DIR="${SDK_DIR:-/home/sheldeng/zephyr-sdk-0.16.9}"                     # Zephyr SDK（已跑过 setup.sh -c 注册 cmake 包）
BOARD="${BOARD:-rtl87x3g_evb}"
PORT_DEFAULT="${PORT_DEFAULT:-/dev/ttyUSB0}"                # WSL2 下需要先用 usbipd-win 把 USB 转串口设备挂进来
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

# Designer 新版把用户源文件放进 src/user/，romfs_layout.h 也跟着搬了家。
# 两个位置都可能存在；src/ 那份会遮蔽 src/user/，优先读实际生效的那份。
resolve_layout_h() {
    local a="$APP_DIR/app/designer/src/romfs_layout.h"
    local b="$APP_DIR/app/designer/src/user/romfs_layout.h"
    if [ -f "$a" ]; then echo "$a"
    elif [ -f "$b" ]; then echo "$b"
    elif [ -d "$(dirname "$b")" ]; then echo "$b"
    else echo "$a"; fi
}

# pack.py fwcheck 返回 2 = ELF 里的 romfs 挂载地址还是旧的。
# 其它返回码（0/无法校验）都当作 OK，不让环境缺失阻断打包。
#
# 显式传 --elf：build_bank<N>_wifi 和 build_bank<N> 会同时存在，让 pack.py 自己
# 挑很容易挑到没在用的那个，于是永远判 stale、每轮都重编，直到轮数用尽报错。
fw_mount_ok() { # $1=bank_sel $2=offset
    local rc=0
    python3 "$PYTHON_PACK" fwcheck --bank "bank$1" --offset "$2" \
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
            # 显式传过 --snippet（值为空表示「不要默认 wifi」），后面不再自动加 wifi
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

# 默认 snippet = wifi；显式传过任何 --snippet（含空值）就不再自动加
if [ "$SNIPPETS_EXPLICIT" -eq 0 ] && [ ${#SNIPPETS[@]} -eq 0 ]; then
    SNIPPETS=(wifi)
fi

# ===== 前置检查 =====
[ -x "$VENV_PYTHON" ] || { echo "找不到 venv python: $VENV_PYTHON" >&2; exit 1; }
[ -x "$SDK_DIR/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc" ] || { echo "找不到 Zephyr SDK 工具链: $SDK_DIR" >&2; exit 1; }
for b in 0 1; do
    [ -f "$APP_DIR/snippets/flash_32M_bank$b/snippet.yml" ] || {
        echo "找不到本工程的分区表 snippet: snippets/flash_32M_bank$b/snippet.yml" >&2
        exit 1
    }
done

# ===== 环境变量 =====
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
export ZEPHYR_SDK_INSTALL_DIR="$SDK_DIR"
echo "[env]   VARIANT=zephyr  SDK=$SDK_DIR"

snip_suffix=""
if [ ${#SNIPPETS[@]} -gt 0 ]; then
    snip_suffix="_$(IFS=_; echo "${SNIPPETS[*]}")"
    echo "[snip]  ${SNIPPETS[*]}"
fi

# ===== 耗时统计（bash 没有 Stopwatch，用 SECONDS 内建变量，够用）=====
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

# ===== 单次编译(+可选烧录)。$1: ''=默认, '0', '1' =====
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

    # 校验分区表真的来自本工程：Zephyr 对同名 snippet 是各追加一次 overlay
    # （不是后者覆盖前者），若 realtek-app/snippets 下也有一份，两份会同时生效。
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

    # 烧录地址以 flash_map.ini 为准（无 bank 的普通编译按 bank0 处理）
    addr=$(grep -E "^BANK${bank_sel:-0}_APP_ADDR=" "$APP_DIR/flash_map.ini" \
           | cut -d= -f2 | tr -d '[:space:]' || true)
    [ -n "$addr" ] || {
        echo "flash_map.ini 里没有 BANK${bank_sel:-0}_APP_ADDR" >&2
        return 1
    }
    echo "[build] OK -> $bin (@ $addr)"

    # ===== 可选：合并 romfs 进 app 镜像 =====
    #
    # romfs 在镜像内的偏移 ROMFS_OFFSET 写在 app/designer/src[/user]/romfs_layout.h，
    # 而它又取决于 app 的体积 —— 所以「先编译才知道偏移，已知偏移才能打包，
    # 打包后固件才正确」是个环。这里迭代到收敛：打包 → 偏移变了就重编 → 再打包。
    # 实测增量重编 ~15s，通常 2 轮收敛。
    if [ "$PACK" -eq 1 ]; then
        [ -f "$ROMFS_BIN" ] || { echo "找不到 romfs: $ROMFS_BIN" >&2; return 1; }
        [ -n "$bank_sel" ] || { echo "--pack 需要显式 --bank 0|1|both" >&2; return 1; }

        local layout_h
        layout_h=$(resolve_layout_h)
        local merged="$APP_DIR/bin/bank${bank_sel}/app_bank${bank_sel}_packed.bin"
        # 单独烧录用：已重定位、4K 对齐落点的 romfs 副本
        local romfs_only="$APP_DIR/bin/bank${bank_sel}/app_romfs_bank${bank_sel}_reloc.bin"
        local round=0
        while :; do
            round=$((round+1))
            local before after
            before=$(grep -oE 'ROMFS_OFFSET 0x[0-9A-Fa-f]+' "$layout_h" 2>/dev/null | awk '{print $2}')
            local pack_mp_args=()
            [ "$PACK_MP" -eq 1 ] && pack_mp_args+=(--mp)

            # --mp: 追加 512B MP 头。mpcli 靠它识别镜像类型/版本；
            #       烧录地址仍来自 -A 或 json（不来自 MP 头）。
            measure_phase "pack bank${bank_sel} (round $round)" \
                "$PYTHON_PACK" app \
                    --bank "bank${bank_sel}" --app "$bin" --romfs "$ROMFS_BIN" \
                    --out "$merged" --sign "${pack_mp_args[@]}" \
                    --romfs-out "$romfs_only" \
                    ${ROMFS_OFFSET_ARG:+--romfs-offset $ROMFS_OFFSET_ARG}
            after=$(grep -oE 'ROMFS_OFFSET 0x[0-9A-Fa-f]+' "$layout_h" 2>/dev/null | awk '{print $2}')

            # 收敛条件有两个：偏移没变，且固件 ELF 里真的挂着这个地址。
            # 后者是必需的：romfs_layout.h 换过位置（src/ -> src/user/）后，
            # ninja 里旧的依赖已失效，偏移没变也不会重编 Mi_eBadgeEntry.c，
            # ELF 会一直停在旧挂载地址 —— 镜像对、固件错，UI 全黑。
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
            # 头文件可能刚换了位置，touch 源文件确保 Mi_eBadgeEntry.c 真的重编。
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

    # ===== 可选烧录 =====
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

# ===== 编译（结束后恢复原目录，并始终打耗时汇总）=====
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
