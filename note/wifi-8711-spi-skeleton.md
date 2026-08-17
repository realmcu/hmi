# 外挂 8711 Wi-Fi（AT-over-SPI）挂载骨架

2026-08-14。**只搭挂载骨架**：devicetree + Kconfig + CMake 打通，能在 build 期抓住 overlay 写错、在 boot 期抓住总线没起来。**不含任何跟芯片通信的代码**，AT 引擎 / SPI DMA 传输 / socket 流程全部待移植（见文末待办）。

两条编译都过（`EXITCODE=0`，2026-08-14）：baseline 不带 snippet、以及 `-S wifi_8711` pristine 445/445。

## 为什么必须动设备树

`CONFIG_SPI_RTL87X3G` 虽然 `default y`，但 `depends on DT_HAS_REALTEK_RTL87X3G_SPI_ENABLED`。而 `rtl87x3g.dtsi:343-396` 里 5 个 SPI 节点（`spi0` / `spi0_slave` / `spi1` / `spi1_hs` / `spi2`）**全是 `status="disabled"`**。不给 overlay，SPI 驱动根本不参与编译，`CONFIG_SPI=y` 也没用。

另外 `realtek,rtl87x3g-spi.yaml` 把 `pinctrl-0` 和 `pinctrl-names` 列为 **required**——想 enable 控制器就必须同时给出 pinctrl group，两件事没法拆开做。

## 引脚分配（用户指定）

| 信号 | Pad | 说明 |
|---|---|---|
| SPI CLK | P4_2 | `HS_P4_2_SPI1_CLK` |
| SPI MISO | P4_3 | `HS_P4_3_SPI1_MISO` |
| SPI MOSI | P4_4 | `HS_P4_4_SPI1_MOSI` |
| SPI CS | P4_5 | `HS_P4_5_SPI1_CSN`（硬件 SS_N，不是 GPIO CS） |
| BLE→WIFI IRQ (M2S) | P4_1 | `&gpiob 8`，`GPIO_ACTIVE_LOW`，输出 |
| WIFI→BLE IRQ (S2M) | P2_7 | `&gpioa 22`，`GPIO_ACTIVE_HIGH`，中断输入 |

pad→GPIO 换算来自 `zephyr/include/zephyr/dt-bindings/pinctrl/rtl87x3g-pinctrl.h`：`P4_1 = 29 /* GPIOB8 */`、`P2_7 = 21 /* GPIOA22 */`。`gpioa` 的 `port=<0>`、`gpiob` 的 `port=<1>`，所以 P4_1 写成 `<&gpiob 8 ...>`。

四条 psel 全部写 `DIR_OUT`（包括 MISO）——这不是笔误，`applications/watch` 和 `applications/bt_audio_trx` 的同名 snippet 一模一样，HS pinmux 下信号方向由 pad 功能决定。

evb 板**没有 `board.h`**（`rtl87x3g_watch` / `rtl87x3g_bt_audio_trx` 才定义 `PIN_SPI1_CLK` 这类宏），所以 `RTL87X3G_PSEL` 第二参数直接写裸 pad 名 `P4_2`，跟本工程已有 overlay 的风格一致。

## 文件清单

```
snippets/wifi_8711/wifi_8711.overlay    DT：spi1_hs + 总线 peer + 握手 GPIO + psram1_nc
snippets/wifi_8711/wifi_8711.conf       CONFIG_WIFI_8711 / SPI / DMA / RING_BUFFER
snippets/wifi_8711/snippet.yml          append EXTRA_CONF_FILE + EXTRA_DTC_OVERLAY_FILE
dts/bindings/spi_m2s2m-gpio.yaml        握手 GPIO 的 binding
dts/bindings/wifi_8711-device.yaml      SPI 总线 peer 的 binding（include spi-device.yaml）
app/wifi_8711/Kconfig                   WIFI_8711 / _ROLE_MASTER / _TEST
app/wifi_8711/CMakeLists.txt            if(CONFIG_WIFI_8711) 包住的 file(GLOB)
app/wifi_8711/wifi_8711.{h,c}           init/ready + 6 条待移植 TODO
app/wifi_8711/wifi_8711_shell.c         wifi8711 init / info / m2s
Kconfig                                 加了 rsource "app/wifi_8711/Kconfig"
```

**零 CMake 改动就能被发现**：`snippets.cmake:60` 把 `APPLICATION_SOURCE_DIR` 自动追加进 `SNIPPET_ROOT`，`pre_dt.cmake:41-47` 同样自动追加 `DTS_ROOT`。所以 `snippets/` 和 `dts/bindings/` 放在 app 根下即可。

## 编译命令

`note/build.ps1` 已加 `-Snippet` 参数（可传多个，与 `-Bank` 叠加）：

```powershell
.\note\build.ps1 -Snippet wifi_8711            # -> build_wifi_8711\,        bin\app.bin
.\note\build.ps1 -Snippet wifi_8711 -Pristine  # 新增 .c 文件后必须这条（见下）
.\note\build.ps1 -Bank 0 -Snippet wifi_8711    # -> build_bank0_wifi_8711\,  bin\bank0\app_bank0.bin
```

snippet 组合参与 build 目录命名，所以带/不带 Wi-Fi 各自独占目录，切换时不会撞上 Zephyr 「snippet 集合变了必须 pristine」的限制，也不会污染默认的 `build\`。

等价的裸 west 命令（脚本内部就是这么拼的）：

```powershell
$env:PYTHONUTF8="1"
$env:ZEPHYR_TOOLCHAIN_VARIANT="zephyr"
$env:ZEPHYR_SDK_INSTALL_DIR="D:\Project\HoneyHmi\bb2u\zephyr-sdk-0.16.8_windows-x86_64_minimal\zephyr-sdk-0.16.8"
Remove-Item Env:\GNUARMEMB_TOOLCHAIN_PATH -ErrorAction SilentlyContinue
python -m west build -b rtl87x3g_evb -d build_wifi_8711 --snippet wifi_8711 -p always
```

`app/wifi_8711/CMakeLists.txt` 用 `file(GLOB)`，**新增 .c 文件后必须 pristine 重编**（跟 `app/protocol/CMakeLists.txt` 同一个坑）。

## 三个非显然的坑

### 1. P4_3 / P4_4 跟触摸屏物理冲突（软件无解）

这两个 pad 同时是 **I2C2_DAT / I2C2_CLK**，接 CST816D 触摸面板。谁最后跑 pinctrl 谁占住 pad，**触摸和 Wi-Fi 在这套连线下不可能同时工作，必须在硬件侧解决**。

overlay 里**故意不去 disable `&i2c2`**：`port/ui/gui_port_indev.c:16-18` 用的是无保护的 `DEVICE_DT_GET(DT_NODELABEL(touch_device))`，没有 `DT_NODE_HAS_STATUS` 包，一 disable 就变成 `__device_dts_ord_NN` 链接错误（跟开 RTC 时踩的是同一类坑），不会干净地编译排除掉。

### 2. `psram1_nc` 节点全仓库没有，`SECTION_PSRAM1_NC` 会静默失效

`SECTION_PSRAM1_NC` 只定义在 `applications/watch/src/app/psram_section.h`，而且被 `DT_NODE_EXISTS(DT_NODELABEL(psram1_nc))` 包住——节点不存在时它**展开成空**，不报错。SPI 那约 114 KB 缓冲会直接落进 82 KB 的 DTCM → 链接溢出。

所以 overlay 里补了个 `zephyr,memory-region`：

```
psram1_nc: memory@22380000 { reg = <0x22380000 DT_SIZE_K(512)>; ... };
```

地址是推出来的，不是猜的：`SPIC1_MEM_BASE = 0x22000000`（`address_map.h:16`）；`gui_port_os.c:24,142-143` 里 `DSP_RSV_SIZE = 512K`、GUI lower heap 从 `SPIC1_MEM_BASE + DSP_RSV_SIZE` 起算 `0x300000`。⇒ `0x22380000..0x22400000` 这 512 KB 空着。而且它落在 `app_lower_init.c:27-34` 的 MPU region 2（`0x22000000..0x22400000`，attr `0x44` = non-cacheable）里，**做 DMA 不需要 `SCB_CleanDCache`**——正好避开 H264 那个 L1 D-cache 回写问题。

链接产物已确认出现 `PSRAM1_NC: 512 KB` region。

### 3. DMA 通道 0/1 是空的

`uart3` 占 2/3（`dmas = <&dma0 2 49 0xa>, <&dma0 3 48 0x10021>`），所以给 SPI 用 0/1，跟 watch 的分配一致。

## 顺手修掉的 baseline 断裂（跟本骨架无关）

搭骨架前跑 baseline 就已经编不过了，这是 V1.2 迁移遗漏的第 7 处调用点：

```
app/designer/src/user/ViewMainFace_user.c:218
  → app/bluetooth/hmi_ble/hmi_ble_central.h:24
  → fatal error: hmi_l2_xfer_client.h: No such file or directory
```

`ViewMainFace_user.c` 的非 simulator 分支里同时引了三个已从构建剔除目录的头：`hmi_ble_central.h`、`hmi_l2.h`（在 `component/protocol/`）、`hmi_l2_cmd_remote.h`（在 `app/l2_handlers/`）。这两个目录的 `CMakeLists.txt` 都只剩注释，既不编源码也不提供 include 路径。

按既有 `#if 0 /* V1.2 migration: ... parked */` 模式封了 4 处（**不删旧代码**）：include 块 + `done_cb`、`prog_arc_timer` 的进度读取、`on_remote_state_changed` 的 getter、`app_remote_ctrl_init` 的三处注册。进度环退化成固定最小角度；三个 remote 回调函数体保留，用 `(void)` 引用压掉 unused warning。note 的停摆表格第 204 行原先声称这处已经封过，实际没有，已改正。

## 待办（全部空着）

### 移植（按此顺序，源在 `applications/watch/src/module/wifi_8711/`，约 3700 行）

1. **上电 / RF bring-up** — 不在 devicetree 里。`applications/eBadge` 用裸 `Pad_Config` / `Pinmux_Config` 做：WIFI_EN 拉低 200 ms → 拉高 → 等 2 s 等 AT 固件起来；然后配 RF switch 的 `EN_EXLNA` / `EN_EXPA`。**8711 自己的时序必须查它的 datasheet，不要照抄别的 IC。**
2. **`app_spi_master_zephyr.c`** — TX ring + ping-pong RX 放 `SECTION_PSRAM1_NC`，async DMA 传输用 idle flag 串行化，S2M 中断 → gpio callback → post 给任务，每次传输前后拉 M2S。**`ring_buf` 控制结构必须留在片内 RAM**：PSRAM 上没有 LDREX/STREX。
3. **`app_spi_atcmd.c`** — `[AT(2)][len(2)][data(N)][crc32(4)][pad]` 组帧，`SPI_FRAME_CRC_EN 0`（ameba slave 不追加校验），命令/响应表带 per-command 超时，以及两段式 `AT+SKTSENDRAW`（发命令 → 等 `">>>"` → 推裸数据 → 等 `"OK"`）。`SPI_XMIT_SIZE = 16 KiB`。
4. **摘掉 watch 专属依赖** — `app_cmd.h` / `app_dlps.h` / `trace.h` / `rtk_errno.h`；`psram_section.h` 在 `applications/watch/src/app/` 下，得拷过来或把 `SECTION_PSRAM1_NC` 宏内联。
5. **`ebadge_port_tcp` 做成 AT adapter** — `listen` → `AT+SKTCFG` + `AT+SKTSERVER`；`on_data` ← 非请求 RX 帧；`send` → `AT+SKTSENDRAW`；`close` → `AT+SKTDEL`。port_tcp 契约要求每次回调攒到 ≥4 KB，16 KB 的 SPI 帧天然满足。
6. **`ebadge_port_softap` 是未解问题** — watch 模块里**只找到 `AT+WLCONN`（station 模式），没有 AP 模式命令**。而 eBadge V1.2 需要设备自己**做 AP**。动手前先对着 8711 的 AT 固件文档确认 port_softap 到底能不能实现。

### 硬件 / 验证

- P4_3/P4_4 与触摸屏的 pad 冲突要在硬件上定方案（改线 or 二选一）。
- 骨架只做到 boot 期检查，`wifi8711 init` / `info` / `m2s <0|1>` 三条 shell 命令还没上板跑过。`m2s` 那条配合示波器探 P4_1 用来区分「我们的 pin 配错了」和「芯片不应答」。
- TCP/IP 栈在 8711 固件里，SoC 上**没有 lwIP、没有 Zephyr `net_if`**，socket 全是 AT 命令。
