# RTL87X3E HMI Dashboard

基于 Realtek RTL87X3E SoC 的 HMI 固件开发包，面向带有显示和无线连接能力的嵌入式 HMI 应用。

## 功能特性

| 功能 | 状态 |
|---|---|
| HoneyGUI 显示引擎 | ✅ 可用 |
| 蓝牙（BLE + BR/EDR） | ✅ 可用 |
| OTA 空中升级 | ✅ 可用 |
| 4 种构建模式（src/lib × bank0/bank1） | ✅ 可用 |
| 双工具链支持（GCC / Keil MDK） | ✅ 可用 |

## 硬件规格

- **SoC**：Realtek RTL87X3E
- **显示**：ST7265，800 × 480，RGB 接口
- **按键**：8 键输入（ADC 方式）
- **音频**：AW87390 智能功放（I2C）
- **接口**：UART、I2C

## 工具链依赖

| 工具 | 版本要求 | 用途 |
|---|---|---|
| `python3` | ≥ 3.8 | west 运行环境 |
| `west` | ≥ 1.2 | Workspace 管理器及自定义命令 |
| `git` | ≥ 2.20 | 版本控制 |
| `arm-none-eabi-gcc` | ≥ 10.3 | 交叉编译器（GCC 构建） |
| `cmake` | ≥ 3.20 | 构建系统（GCC 构建） |
| `ninja` | ≥ 1.10 | 并行构建（GCC 构建） |
| `mpcli` | — | 固件下载工具 |

> Keil MDK 用户无需安装 ARM GCC / CMake / Ninja，直接在 IDE 中打开 MDK 工程编译即可。

## 组件仓库

| 仓库 | 本地路径 | 说明 |
|---|---|---|
| [rtl87x3ep-hmi-sdk](https://gitee.com/realmcu/rtl87x3ep-hmi-sdk) | `sdk/` | 核心 SDK：HAL 驱动、蓝牙协议栈、系统服务、工具链等 |
| [hmi-dashboard](https://gitee.com/realmcu/hmi/tree/rtl8773e-dashboard/) | `sdk/board/evb/hmi_dashboard/` | HMI 应用层、BSP、GUI 移植、构建配置 |
| [HoneyGUI](https://gitee.com/realmcu/HoneyGUI) | `sdk/src/sample/gui/` | GUI 引擎：控件库、字体引擎、动画 |
| [wearable](https://gitee.com/realmcu/wearable) | `sdk/src/app/Wearable/` | Wearable 应用层代码 |
| [display](https://gitee.com/realmcu/display) | `sdk/src/mcu/display/` | LCD 显示驱动库 |

## 快速上手

### 1. 初始化 workspace

```bash
# 1. 创建工作目录（名称可自定义）
mkdir hmi
cd hmi

# 2. 初始化 west 工作空间
west init -m https://gitee.com/realmcu/hmi-manifest.git --mr master --mf rtl8773e-dashboard-gitee.yml .

# 3. 同步所有子项目
west update
```

### 2. 构建固件

| 模式 | 命令 | 编译速度 | 适用场景 |
| --- | --- | --- | --- |
| 源码模式 | `west build` 或 `west build -m src_bank0` | 慢（首次 5–10 分钟） | 需调试 GUI 源码 |
| 库模式 | `west build -m lib_bank0` | 快（约 1 分钟） | 日常迭代（推荐） |

OTA B 槽将 `bank0` 替换为 `bank1`（`src_bank1` / `lib_bank1`）。

```bash
west build              # 默认：源码模式，bank0（OTA A 槽）
west build -m lib_bank0 # 库模式，bank0（链接预编译 libgui.a，迭代更快）
```

### 3. 烧录固件

```bash
west flash                  # 默认串口 COM3，烧录 src_bank0 镜像
west flash -p COM5          # 指定串口
west flash -m src_bank1     # 烧录 bank1 镜像（须与 west build -m 保持一致）
```

> `-m` 须与构建时的 mode 保持一致，否则将烧录错误槽位的固件。

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

如需直接调用 CMake 构建，参见 [`gcc/README_CN.md`](gcc/README_CN.md)。

### Keil MDK

用 Keil MDK 5 打开 `mdk/project.uvprojx`，在 IDE 内直接构建。

若需要重新生成工程文件（如配置发生变动）：

```bash
scons --target=mdk5
```

## 目录结构

> 以下路径相对于 `sdk/board/evb/hmi_dashboard/`（West workspace 内的主应用仓库根目录）。

```
hmi_dashboard/
├── src/
│   ├── application/         # 应用入口、功能开关、面板初始化
│   ├── bsp/                 # 启动代码、系统级初始化
│   ├── gui_lib/             # HoneyGUI 预编译库及头文件
│   ├── ports/
│   │   └── realgui_port/    # HoneyGUI 平台适配层
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
