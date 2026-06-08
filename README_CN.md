# RTL87X3E HMI Dashboard

基于 Realtek RTL87X3E SoC 的 HMI 固件开发包，面向带有显示和无线连接能力的嵌入式 HMI 应用。

## 功能特性

| 功能 | 状态 |
|---|---|
| HoneyGUI 显示引擎 | ✅ 可用 |
| LVGL 显示引擎 | 🚧 开发中 |
| 蓝牙（BLE + BR/EDR） | 🚧 开发中 |
| BLE 私有协议 | 🚧 开发中 |
| OTA 空中升级 | 🚧 开发中 |
| 4 种构建模式（src/lib × bank0/bank1） | ✅ 可用 |
| 双工具链支持（GCC / Keil MDK） | ✅ 可用 |

## 硬件规格

- **SoC**：Realtek RTL87X3E
- **显示**：ST7265，800 × 480，RGB 接口
- **按键**：8 键输入（ADC 方式）
- **音频**：AW87390 智能功放（I2C）
- **接口**：UART、I2C

## 工具链依赖

| 工具 | 用途 |
|---|---|
| `arm-none-eabi-gcc` | 交叉编译器 |
| `cmake` + `ninja` | 构建系统 |
| `west` | Workspace 管理器及自定义命令 |
| `python3` | menuconfig 脚本及 west 扩展 |
| `mpcli` | 固件下载工具 |

## 快速上手

### 1. 初始化 workspace

```bash
west init -l .manifest
west update
```

初始化完成后，日常同步请用 `west sync` 代替 `west update`。
它会先强制更新 manifest 仓库，再执行 `west update` 和 submodule 更新。

### 2. 构建固件

```bash
# 默认：源码模式，bank0（OTA A 槽）
west build

# 库模式，bank0——链接预编译 libgui.a（迭代更快）
west build -m lib_bank0
```

其他 mode：`src_bank1` / `lib_bank1`（OTA B 槽），用法同上替换 `-m` 参数。

### 3. 烧录固件

```bash
west flash            # 默认串口 COM3
west flash -p COM5    # 指定串口
```

## 构建命令参考

| 命令 | 说明 |
|---|---|
| `west build` | 默认构建（等价 `-m src_bank0`） |
| `west build -m lib_bank0` | 库模式，A 槽（预编译 GUI，迭代更快） |
| `west build -m src_bank1` | 源码模式，B 槽 |
| `west build -m lib_bank1` | 库模式，B 槽 |
| `west build -c` | 清空后重新构建 |
| `west build -j 8` | 指定并行 job 数 |
| `west clean` | 删除 build 目录 |
| `west clean --all` | 同时删除 build 目录和 bin 输出 |
| `west flash` | 通过串口烧录（默认 COM3，src_bank0） |
| `west flash -p <port>` | 指定串口烧录 |
| `west flash -m <mode>` | 烧录指定 mode 的镜像（须与 build 时一致） |
| `west size` | 查看 ELF 各 section 大小 |
| `west sync` | 强制更新 manifest 仓库 + `west update` + submodule 更新 |
| `west info` | 显示 workspace 及构建状态 |

### CMake 直接调用（备用方式）

在 `honeycomb/sdk/` 目录下执行：

```bash
# 默认：源码模式，bank0
cmake -G Ninja \
  -Dkconfig_path=board/evb/hmi_dashboard/gcc/defconfig.RTL8773E.hmi_dashboard_src_bank0 \
  -DIS_CHECK_FLOW=off \
  -Dcompile_lib_only=OFF \
  -B build
cmake --build build
```

其他 mode 将末尾替换：`src_bank0` → `src_bank1` / `lib_bank0` / `lib_bank1`。

### Keil MDK

用 Keil MDK 5 打开 `mdk/project.uvprojx`，在 IDE 内直接构建。

## 目录结构

```
hmi_dashboard/
├── src/
│   ├── application/         # 应用入口、功能开关、面板初始化
│   ├── bsp/                 # 启动代码、系统级初始化
│   ├── gui_lib/             # HoneyGUI 预编译库及头文件
│   ├── ports/
│   │   ├── realgui_port/    # HoneyGUI 平台适配层
│   │   └── lvgl_port/       # LVGL 平台适配层（开发中）
│   ├── hmi_rtk_bt/          # 蓝牙协议栈集成（开发中）
│   └── protocol/            # BLE 私有协议实现（开发中）
├── gcc/                     # 链接脚本、defconfig、构建输出
├── mdk/                     # Keil MDK 工程文件
├── cfg/                     # 硬件配置（rtl87x3ep）
├── inc/                     # 芯片 memory config 头文件
├── download/                # 固件下载工具
├── west_commands_extention/ # West 自定义命令定义
├── board.h                  # 引脚映射与外设配置
├── mem_config.h             # 内存布局（DTCM1 / ITCM1）
├── menu_config.h            # GUI 菜单配置
└── version.h                # 固件版本号
```

## 模块说明

### `src/application/`

应用入口（`main.c`）、面板初始化，以及集中管理功能开关的头文件（`app_flags.h`），
用于在编译时控制各子系统的启用状态。

### `src/bsp/`

板级支持包：RTL87X3 内核的启动代码及系统级外设初始化。

### `src/gui_lib/`

HoneyGUI 预编译库（GCC 对应 `libgui.a`，Keil 对应 `gui.lib`）及配套公共头文件，
在库模式构建时使用，可显著缩短编译时间。

### `src/ports/realgui_port/`

HoneyGUI 与硬件之间的平台适配层，涵盖显示控制器、输入设备、文件系统、
操作系统接口及 Flash 转换层。

### `src/ports/lvgl_port/` *（开发中）*

LVGL 平台适配层，包含显示、输入设备和文件系统适配。

### `src/hmi_rtk_bt/` *（开发中）*

蓝牙协议栈集成，覆盖：
- **BLE**：GAP、GATT profile、私有 GATT 服务
- **BR/EDR**：A2DP、AVRCP、HFP、SPP、PAN

### `src/protocol/` *（开发中）*

BLE 私有通信协议实现（L0 / L1 / L2 三层架构）。

## 关键配置文件

| 文件 | 用途 |
|---|---|
| `app_flags.h` | 全应用功能开关 |
| `board.h` | GPIO 引脚映射与外设配置 |
| `mem_config.h` | DTCM / ITCM 内存区域布局 |
| `menu_config.h` | GUI 菜单设置 |
| `gcc/defconfig.*` | GCC 构建的 Kconfig 预设 |
| `version.h` | 固件版本号（`VERSION`、`BUILD_NUM`） |

## 版本信息

当前固件版本定义在 `version.h` 中：

```c
#define VERSION     "3.14.8"
#define BUILD_NUM   72
```
