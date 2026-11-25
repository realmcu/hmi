# RTL87x2G LVGL HMI 示例工程

基于 **LVGL v9.4** 的 RTL87x2G 图形界面示例工程，同一套移植层支持 QSPI 和 RGB 两种显示屏。

> 工程目录名为 `lvgl_8772g`，芯片类型是 `rtl87x2g`，因此 `subsys/display` 下使用 `8762G` 目录的驱动。

## 硬件规格

| 项目 | 规格 |
| --- | --- |
| 主控 | RTL87x2G（Armv8.1-M，DSP + MVE + FPU） |
| 显示屏 1 | ICNA3311，280 × 456，RGB565，QSPI（DBIC） |
| 显示屏 2 | ST7701S，480 × 480，RGB565，RGB（eDPI） |
| 触摸屏 | GT911，I2C0，从机地址 0x14 |
| PSRAM | 4 MB @ `0x08000000`（W955D8MKY），OPI 80 MHz |

触摸 IC 是 **GT911**，不是 ZT2717。`subsys/display` 里两个驱动用的引脚相同（RST `P2_2` / INT `P2_3` / SCL `P2_4` / SDA `P2_5`），但选错驱动时 `rtk_touch_hal_init()` 只会读回全 0，很容易误判成"总线不通"。

## 软件组成

| 项目 | 说明 |
| --- | --- |
| GUI | LVGL v9.4 |
| GUI 配置 | `src/lvglv9_port/lv_conf.h` |
| 显示移植 | `src/lvglv9_port/lv_port_disp.c` |
| 显示驱动选择 | `src/lvglv9_port/display_drv.h` |
| 触摸移植 | `src/lvglv9_port/lv_port_indev.c` |
| 面板 / 触摸选择 | `proj/menu_config.h` |
| 应用入口 | `src/application/main.c` → `lvgl_init.c` → `src/lvglv9_port/lvgl_demo.c` |

关键配置：

- 颜色格式：RGB565（`LV_COLOR_DEPTH 16`），QSPI 与 RGB 两种屏都不需要字节交换
- 绘制缓冲：双缓冲、整屏、`LV_DISPLAY_RENDER_MODE_DIRECT`，放在 PSRAM
- LVGL 堆：`LV_MEM_SIZE` 128 KB（TCM 静态池）+ `lv_mem_add_pool()` 追加 1 MB PSRAM 池
- 刷新周期：33 ms（`LV_DEF_REFR_PERIOD`）
- `LV_USE_OS = LV_OS_NONE`：LVGL 跑在独立任务里，主循环用 `os_delay()` 让出 CPU
- `LV_USE_PERF_MONITOR = 1`：benchmark 的 FPS 数据依赖它

工程默认运行 LVGL 官方 benchmark（`lvgl_demo.c` 中调用 `lv_demo_benchmark()`），改为 `lv_demo_widgets()` 只需替换该调用。

## 屏幕与触摸选择

都在 `proj/menu_config.h` 里，**切屏时两处都要改**。

LCD 驱动：

```c
#define CONFIG_REALTEK_LCD_CONFIG_DRIVER 8   /* 8 = ST7701S 480x480 RGB
                                              * 2 = ICNA3311 280x456 QSPI */
```

`CONFIG_REALTEK_LCD_CONFIG_INTERFACE`（DBIB / DBIC / EDPI）由上面的驱动号**自动推导**，不需要手动改：

| 驱动号 | 面板 | 自动推出的接口 |
| --- | --- | --- |
| 0 / 1 | NT35510 / ST7796（DBIB 类） | DBIB |
| 2 | ICNA3311 280 × 456 QSPI | DBIC |
| 8 | ST7701S 480 × 480 RGB | EDPI |

触摸驱动：

```c
#define CONFIG_REALTEK_TOUCH_CONFIG_DRIVER 1  /* 0 = ZT2717, 1 = GT911, 2 = CST836U
                                               * 本板是 GT911 */
```

新增面板时，只需在 `src/lvglv9_port/display_drv.h` 里加一个 `#elif`（包含对应头文件并给出 `DISPLAY_DRV_WIDTH` / `DISPLAY_DRV_HEIGHT`），`lv_port_disp.c` 不用改。

## 编译

### Keil MDK

工程文件是 `scons --target=mdk5` 生成的，**不要直接手改 `proj/mdk/project.uvprojx`**（下次生成会被覆盖）。模板在 `proj/mdk/template.uvprojx`。

```bash
cd applications/lvgl_8772g/proj
scons --target=mdk5
```

然后打开 `proj/mdk/project.uvprojx` 编译。编译后由 `after_build_script.bat` 调 `prepend_header` 生成带 MP 头的 `bin/app_ns_MP.bin`。

> 每次修改 `proj/menu_config.h` 后都要重新执行 `scons --target=mdk5`：Keil 的文件列表（比如 `rtl_lcdc_edpi.c` 是否参与编译）是按宏生成的。

### GCC / SCons

```bash
cd applications/lvgl_8772g/proj
scons -j8
```

输出 `app_MP_*.bin`，可直接烧录。工具链默认使用 `Arm GNU Toolchain arm-none-eabi/13.2 Rel1`，用环境变量 `ARM_GCC` 可覆盖。构建前会用编译器预处理 `gcc/app.ld`（包含 `mem_config.h` / `flash_map.h`）生成 `gcc/app.ld.gen`，内存布局与 Keil 的 `app.sct` 保持一致。

## PSRAM 分配

LVGL 的绘制缓冲和堆都放在 PSRAM，地址是硬编码的，改动时要和 `lv_port_disp.c`、`lvgl_demo.c` 同时对齐：

| 地址 | 大小 | 用途 |
| --- | --- | --- |
| `0x08000000` | 整屏一帧 | draw buffer 1 |
| `0x08080000` | 整屏一帧 | draw buffer 2 |
| `0x08100000` | 1 MB | LVGL 堆（`lv_mem_add_pool`） |
| `0x08200000` | 2 MB | 链接脚本 `PSRAM_SECTION`，勿占用 |

480 × 480 × 2 = 450 KB，两帧占 `0x08000000` ~ `0x080E1000`，与堆不重叠。

## 显示数据通路（移植时的关键约束）

这三条是实测结论，改动移植层前务必了解：

1. **刷屏必须用 `rtk_lcd_hal_update_framebuffer()`**，不能使用 `rtk_lcd_hal_start_transfer()` + `rtk_lcd_hal_transfer_done()`。后者在 ICNA3311 驱动上只能成功传输一次，第二次会永远死等在 `auto_write_start`。
2. **绘制缓冲不能放在 `.bss`**（落在 ITCM1 `0x0010xxxx`），LCDC 的 DMA 读不到，表现为传输永不结束。实测可用的区域：**PSRAM** 和 **DTCM**（栈所在区）；`DATA_SRAM`（`0x00200000`）也不可用。
3. **两种屏的 flush 逻辑相同**：RGB 屏的 `rtk_lcd_hal_set_window()` 是空操作，`update_framebuffer()` 负责切换扫描地址；QSPI 屏则用 `set_window()` 设置 GRAM 窗口。因此不需要 `#if` 分支。

## Flash 布局

OTA Bank0 起始 `0x04012000`，APP code 起始 `0x0405F000`，详细布局见 `proj/flash_map.h`。

> **已知问题**：`fmc_flash_nor_clock_switch()` 切换 160 MHz 失败，flash 实际停在 40 MHz，`flash_nor_try_high_speed_mode()` 却返回成功。不影响功能，但从 flash 取指会慢一倍以上，benchmark 分数明显偏低。板上 NOR flash 为 32 MB，而 `flash_map.h` 的 `FLASH_MAX_SIZE` 仍是 16 MB，可能需要开启 4 字节地址模式（`flash_nor_set_addr_mode()`）。

## 已知问题

- flash 时钟停在 40 MHz（见上）
- `src/lvglv9_port/lv_port_disp.c` 中保留了 `f%d: flush` 调试打印，前 8 帧及之后每 60 帧输出一条
- DLPS 未启用（`board.h` 中 `DLPS_EN` 为 0），面板和触摸暂未注册休眠回调

## 目录说明

```
.
├── proj/                       工程与构建配置
│   ├── SConstruct              scons 入口
│   ├── SConscript              板级 group（soc 宏 + 公共 include 路径）
│   ├── menu_config.h           面板 / 触摸 / 外设选择（Keil 可图形化配置）
│   ├── menu_config.py          工具链参数
│   ├── mem_config.h            TCM 内存划分
│   ├── flash_map.h             Flash 布局
│   ├── board.h                 DLPS 等板级开关
│   ├── gcc/app.ld              GCC 链接脚本（预处理后使用）
│   └── mdk/
│       ├── template.uvprojx    Keil 工程模板
│       └── app.sct             Keil 分散加载文件
└── src/
    ├── application/            应用入口（main.c、lvgl_init.c）
    ├── lvglv9_port/            LVGL 移植层（显示、触摸、demo、lv_conf）
    ├── dlps/                   低功耗（当前未启用）
    ├── ble_ota/                BLE OTA 示例（当前未启用）
    ├── ble_peripheral/         BLE 外设示例（当前未启用）
    └── fs/                     文件系统相关
```

面板驱动与触摸驱动位于 `subsys/display/device/general/{lcd,touch}/8762G/`，属于共享仓库，应用侧问题应优先在 `src/lvglv9_port/` 内解决。
