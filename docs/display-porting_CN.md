# 显示屏移植指南

RTL87X3E HMI Dashboard 默认使用 ST7265、800 × 480、RGB 显示屏。如果目标屏幕的 Driver IC、分辨率或接口不同，需要适配 Panel Driver、Dashboard 板级配置以及 HoneyGUI Display Port。

如果分辨率发生变化，还需要同步调整 Designer UI，参见 [UI 工程移植指南](ui-porting_CN.md)。

本文路径均相对于 West workspace 根目录。

## 1. 判断修改范围

| 目标屏幕变化 | Panel Driver | `gui_port_dc.c` |
| --- | --- | --- |
| Driver IC 不同，接口和分辨率不变 | 切换或新增 | 通常不需要 |
| 分辨率不同，接口仍为 RGB | 切换或新增，并修改时序 | 检查 buffer 大小和硬编码尺寸 |
| RGB 屏更换为 SPI/QSPI 屏 | 切换或新增 | 必须适配刷新路径 |
| SPI/QSPI 屏更换为同类接口屏 | 切换或新增 | 检查窗口、分段和完成通知 |

> “接口类型相同”并不保证 Display Port 一定兼容。如果像素格式、stride、屏幕方向、全屏/局部刷新方式或同步机制改变，仍需检查 `gui_port_dc.c`。

## 2. 移植前收集屏幕参数

从原理图和屏幕规格书确认：

- Driver IC 型号；
- 水平和垂直分辨率；
- 接口类型：RGB、SPI、QSPI 等；
- 输入/输出像素格式，例如 RGB565 或 RGB888；
- RGB 的 pixel clock、HSYNC、VSYNC、DE 极性及 porch 参数；
- SPI/QSPI 的 lane 数、command format、最高时钟及 TE 要求；
- RESET、背光、电源和接口引脚；
- 扫描方向、旋转方式和 RGB/BGR 顺序；
- 是否支持或要求局部刷新；
- framebuffer 数量及可用 PSRAM/SRAM 容量；
- 触摸 IC 的分辨率、方向和坐标映射。

建议先完成纯色画面和刷新验证，再接入完整 UI。这样可以把 Panel Driver 问题与 UI 布局问题分开定位。

## 3. 适配 Panel Driver

Driver 所在仓库和 RTL8773E 目录为：

```text
sdk/src/mcu/display/
├── Kconfig
└── device/general/lcd/8773E/
    ├── CMakeLists.txt
    ├── SConscript
    ├── lcd_st7265_800480_rgb.c
    └── lcd_st7265_800480_rgb.h
```

### 3.1 仓库中已有目标 Driver IC

1. 在 `sdk/src/mcu/display/device/general/lcd/8773E/` 中查找目标 Driver IC 和接口对应的实现。不要只按 IC 名称判断；同一 IC 可能有不同分辨率或接口版本。
2. 在 `sdk/src/mcu/display/Kconfig` 中找到该 driver 对应的 `CONFIG_REALTEK_LCD_*` 选项。
3. 确认该选项已经在以下两个构建文件中映射到正确源文件：
   - `sdk/src/mcu/display/device/general/lcd/8773E/CMakeLists.txt`（GCC）；
   - `sdk/src/mcu/display/device/general/lcd/8773E/SConscript`（Keil MDK）。
4. 关闭默认 ST7265 选项并启用目标 driver。一次构建只应启用一个实现 `rtk_lcd_hal_*` 接口的 Panel Driver，否则会产生重复符号或选中错误实现。
5. 按目标硬件核对 driver 中的引脚、reset/power/backlight 流程、时钟、时序、像素格式和扫描方向。

#### GCC 配置

Dashboard 的四种构建模式分别使用以下 defconfig：

```text
sdk/board/evb/hmi_dashboard/gcc/
├── defconfig.RTL8773E.hmi_dashboard_src_bank0
├── defconfig.RTL8773E.hmi_dashboard_src_bank1
├── defconfig.RTL8773E.hmi_dashboard_lib_bank0
└── defconfig.RTL8773E.hmi_dashboard_lib_bank1
```

修改实际使用模式对应的 defconfig。例如，默认配置包含：

```ini
CONFIG_REALTEK_DISPLAY=y
CONFIG_REALTEK_DISPLAY_ENABLE_LCDC=y
CONFIG_REALTEK_LCD_ST7265_800_480_RGB=y
```

将默认 LCD 选项改为 `n` 或删除该行，再将目标 driver 的 Kconfig 选项设为 `y`。配置名以 `sdk/src/mcu/display/Kconfig` 及生成的 `config.h` 为准。

#### Keil MDK 配置

1. 在 Keil 中打开 `sdk/board/evb/hmi_dashboard/menu_config.h`；
2. 使用 **Configuration Wizard** 关闭默认 LCD driver，并启用目标 driver；
3. 保存并关闭 Keil 工程；
4. 在 `sdk/board/evb/hmi_dashboard/` 下重新生成工程：

```bash
scons --target=mdk5
```

5. 重新打开 `mdk/project.uvprojx`，执行 *Rebuild all target files*。

如果不使用 SCons，也可以直接在 Keil 工程中手动切换 driver：

1. 打开 *Project* → *Manage* → *Project Items*，找到 `lcd_low_driver` group；
2. 从该 group 中移除当前 Panel Driver 的 `.c` 文件，再加入目标 driver 的 `.c` 文件；
3. 在 *Options for Target* → *C/C++ (AC6)* 中确认目标 driver 头文件目录已加入 Include Paths；
4. 在 `menu_config.h` 中关闭当前 driver 宏并启用目标 driver 宏；
5. 完整 Rebuild，并从编译日志或 map 文件确认只链接了目标 Panel Driver。

手动修改只影响当前 `mdk/project.uvprojx`。之后再次运行 `scons --target=mdk5` 会按 `menu_config.h` 和 `SConscript` 重新生成工程，因此还应让 `menu_config.h`、`SConscript` 与手动选择保持一致。

如果目标 driver 尚未出现在 Configuration Wizard 中，还需要为 `menu_config.h` 增加对应选项，并确认 `SConscript` 使用相同的宏名。

### 3.2 仓库中没有目标 Driver IC

选择 **SoC 相同且显示接口相同** 的现有 driver 作为模板。接口相同的重要性高于分辨率或厂商相同。

1. 在 `sdk/src/mcu/display/device/general/lcd/8773E/` 新增 `.c` 和 `.h` 文件；
2. 根据规格书实现或调整以下内容：
   - pinmux、power、reset 和 backlight；
   - Panel 初始化命令；
   - RGB/LCDC 或 SPI/QSPI 控制器配置；
   - 分辨率、porch、时钟和信号极性；
   - 像素格式、RGB/BGR 顺序和扫描方向；
   - 全屏/局部窗口设置；
   - framebuffer 更新及传输完成处理；
   - sleep、wake-up 和 power on/off；
3. 保持 Dashboard 使用的公共 HAL 接口可用：

```c
void rtk_lcd_hal_init(void);
void rtk_lcd_hal_update_framebuffer(uint8_t *buf, uint32_t len);
void rtk_lcd_hal_set_window(uint16_t x_start, uint16_t y_start,
                            uint16_t width, uint16_t height);
void rtk_lcd_hal_start_transfer(uint8_t *buf, uint32_t len);
void rtk_lcd_hal_transfer_done(void);
uint32_t rtk_lcd_hal_get_width(void);
uint32_t rtk_lcd_hal_get_height(void);
uint32_t rtk_lcd_hal_get_pixel_bits(void);
```

4. 在 `sdk/src/mcu/display/Kconfig` 中添加唯一的 `CONFIG_REALTEK_LCD_*` 选项；
5. 在 RTL8773E 的 `CMakeLists.txt` 和 `SConscript` 中使用同一个配置宏加入新源文件；
6. 按[仓库中已有目标 Driver IC](#31-仓库中已有目标-driver-ic)的方式选择新 driver。

> `Kconfig`、CMake 和 SCons 中的配置宏必须逐字一致。添加后应检查构建日志或 map 文件，确认实际编译、链接的是目标源文件。

## 4. 调整 Dashboard 板级选择

Panel Driver 被编译进来后，还要检查 Dashboard 侧的板级选择：

- `sdk/board/evb/hmi_dashboard/src/application/app_gui.h`：`TARGET_LCD_DEVICE`、接口类型、宽高和像素格式；
- `sdk/board/evb/hmi_dashboard/board.h`：LCD、触摸、背光和其他复用引脚；
- `sdk/board/evb/hmi_dashboard/src/application/main.c`：启动时调用 `rtk_lcd_hal_init()`；
- `sdk/board/evb/hmi_dashboard/src/ports/realgui_port/gui_port_indev.c`：触摸/按键 driver 选择和坐标来源。

新增屏幕时，应在 `app_gui.h` 中增加清晰、唯一的 device 定义，避免只启用 display 仓库的配置，却仍让 Dashboard 按默认 ST7265 处理。若屏幕方向或分辨率变化，还应同步检查触摸坐标交换、镜像和范围。

## 5. 适配 HoneyGUI Display Port

文件位置：

```text
sdk/board/evb/hmi_dashboard/src/ports/realgui_port/gui_port_dc.c
```

当前实现面向 800 × 480 RGB 屏：HoneyGUI 以若干行组成的 section 渲染，GDMA 将 section 复制到 PSRAM 双 framebuffer，最后通过 `rtk_lcd_hal_update_framebuffer()` 切换整帧。文件中还存在按 800 × 480、RGB565 计算的静态 buffer 和 PSRAM 地址。

### 5.1 仍使用 RGB 接口

如果新 driver 保持相同的 RGB 连续扫描和双 framebuffer 模型，`gui_port_dc.c` 的刷新流程通常可以复用，但必须检查：

- 两个 framebuffer 的起始地址和间距；
- `width × height × bytes_per_pixel × framebuffer_count` 是否超出预留 PSRAM；
- section buffer 宽度、section 高度及字节数；
- `rtk_lcd_hal_get_width()`、`rtk_lcd_hal_get_height()` 和 `rtk_lcd_hal_get_pixel_bits()` 返回值；
- 最后一段不足一个完整 section 时的复制长度；
- cache、DMA 地址、宽度和对齐限制。

不要仅修改 Panel Driver 中的宽高，而保留 `gui_port_dc.c` 中的 `800 * 480 * 2`、`800 * 10 * 2` 等固定值。

### 5.2 RGB 切换为 SPI/QSPI

不能直接沿用 RGB 的 framebuffer 切换逻辑。SPI/QSPI 通常需要在 `gui_port_dc.c` 中实现 command-mode 刷新路径：

1. 根据当前 section 或 dirty rectangle 计算 `(x, y, width, height)`；
2. 调用 `rtk_lcd_hal_set_window()` 设置 Panel 写入区域；
3. 调用 `rtk_lcd_hal_start_transfer()` 发送像素数据；
4. 在复用或覆盖 rendering buffer 前，通过 `rtk_lcd_hal_transfer_done()` 或对应异步完成机制确认传输结束；
5. 根据硬件要求处理 TE、cache clean、DMA 对齐和最大传输长度；
6. 重新设置 `gui_dispdev` 的 framebuffer、section 和回调参数。

可参考 `sdk/src/mcu/display/device/general/lcd/8773E/` 中接口最接近的 QSPI driver，以及 SDK 内其他采用相同控制器和 HoneyGUI 刷新模型的项目。不要只替换头文件和 Kconfig 选项。

## 6. 分辨率和内存

分辨率变化时，至少同步检查：

- Panel Driver 返回的宽、高和 pixel bits；
- `app_gui.h` 中的尺寸和 section 计算；
- `gui_port_dc.c` 中 framebuffer、section buffer 和 PSRAM 地址；
- linker/memory 配置中的 framebuffer 可用范围；
- 触摸坐标范围和方向。

framebuffer 最小容量可按下式估算：

```text
width × height × bits_per_pixel / 8 × buffer_count
```

例如 800 × 480、RGB565、双 framebuffer 需要 1,536,000 字节，尚未包含 section buffer、对齐和其他 GUI 资源。

> 修改 driver 分辨率不会自动缩放 UI。完成显示链路适配后，还需按照 [UI 工程移植指南](ui-porting_CN.md) 调整 Designer 工程。

## 7. 验证清单

- [ ] 构建中只启用了一个 Panel Driver；
- [ ] RESET、电源和背光时序正确；
- [ ] RGB 纯色显示正常；
- [ ] 无花屏、撕裂或周期闪烁；
- [ ] Panel 与 HoneyGUI 的宽高一致；
- [ ] 触摸输入正常；
- [ ] framebuffer 未超出预留内存。
