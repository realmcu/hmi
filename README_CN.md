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

## 工具链依赖

| 工具 | 版本要求 | 检查是否已安装 | 安装 / 下载 |
|---|---|---|---|
| `python3` | ≥ 3.8 | `python3 --version` | [python.org/downloads](https://www.python.org/downloads/) |
| `west` | ≥ 1.2 | `west --version` | `pip install west` |
| `git` | ≥ 2.20 | `git --version` | [git-scm.com/downloads](https://git-scm.com/downloads) |
| `arm-none-eabi-gcc` | ≥ 10.3 | `arm-none-eabi-gcc --version` | [Arm GNU Toolchain 下载](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads) |
| `cmake` | ≥ 3.20 | `cmake --version` | `pip install cmake` 或 [cmake.org/download](https://cmake.org/download/) |
| `ninja` | ≥ 1.10 | `ninja --version` | `pip install ninja` 或 [ninja releases](https://github.com/ninja-build/ninja/releases) |
| `scons` | 4.4.0 | `scons --version` | `pip install scons==4.4.0` |
| `kconfiglib` | - | `python3 -c "import kconfiglib"` | `pip install kconfiglib` |
| Keil MDK5 + ARM Compiler | ARM Compiler ≥ 6.22 | `armclang --version`，或 Keil → *Help* → *About* | [keil.com/download/product](https://www.keil.com/download/product/) |

> - `arm-none-eabi-gcc` / `cmake` / `ninja` **仅 GCC 构建需要**，只用 Keil 的话可以跳过。
> - `scons` / `kconfiglib` 仅在需要重新生成 Keil mdk5 工程时使用（见下方 Keil MDK 章节的配置同步流程），常规构建不需要。
> - Keil 用户只需安装 Keil MDK5，并确保 **ARM Compiler ≥ 6.22**。ARM Compiler 版本过旧或不匹配，
>   是"Keil 编译报大量错误"最常见的原因——遇到编译报错时，请先在 *Project* → *Manage Project Items* →
>   *Project Targets* → *Target* 里确认当前使用的编译器版本，再排查其他问题。

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

> `west update` 会自动拉取下方 [组件仓库](#组件仓库) 中列出的所有仓库，无需手动逐个下载或 `git clone`。

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

用 Keil MDK 5 打开 `sdk/board/evb/hmi_dashboard/mdk/project.uvprojx`，在 IDE 内直接构建。

若需要修改 GUI/功能选项（例如切换 demo、启用某个模块），操作流程如下：

1. 在 Keil 的 *Project* 窗口中双击 `menu_config.h` 打开，切换到编辑器下方的
   **Configuration Wizard** 标签页（该文件由此 wizard 生成，请勿手动改动其中的
   `#define` 行）。
2. 在 wizard 中勾选/切换所需选项，保存（`Ctrl+S`）。Keil 会据此重写 `menu_config.h`。
3. 关闭 Keil 工程（避免文件被占用），打开终端，`cd` 到
   `sdk/board/evb/hmi_dashboard/`（与本 README、`SConstruct` 同级目录）。
4. 重新生成 Keil 工程以应用新配置：

   ```bash
   scons --target=mdk5
   ```

5. 重新打开 `mdk/project.uvprojx`，执行 *Project* → *Rebuild all target files* 完整重新编译。

## 组件仓库

> 以下仓库均由上方[初始化 workspace](#1-初始化-workspace) 中的 `west update` 自动拉取，
> 下表链接仅供参考，通常无需手动 clone。

| 仓库 | 本地路径 | 说明 |
|---|---|---|
| [rtl87x3ep-hmi-sdk](https://gitee.com/realmcu/rtl87x3ep-hmi-sdk) | `sdk/` | 核心 SDK：HAL 驱动、蓝牙协议栈、系统服务、工具链等 |
| [hmi-dashboard](https://gitee.com/realmcu/hmi/tree/rtl8773e-dashboard/) | `sdk/board/evb/hmi_dashboard/` | HMI 应用层、BSP、GUI 移植、构建配置 |
| [HoneyGUI](https://gitee.com/realmcu/HoneyGUI) | `sdk/src/sample/gui/` | GUI 引擎：控件库、字体引擎、动画 |
| [wearable](https://gitee.com/realmcu/wearable) | `sdk/src/app/Wearable/` | Wearable 应用层代码 |
| [display](https://gitee.com/realmcu/display) | `sdk/src/mcu/display/` | LCD 显示驱动库 |

## 芯片专用工具

上表是随 SDK 一起管理的开源仓库；除此之外，RTL87X3EP 芯片专用的工具（芯片配置、
固件下载/烧录、OTA 打包等）统一维护在 [rtl87x3ep-mcu-hmi-sdk-tool](https://gitee.com/realmcu/rtl87x3ep-mcu-hmi-sdk-tool)
仓库，不由 `west update` 自动拉取，需要时自行下载。

| 工具 | 用途 |
|---|---|
| `MCUConfigTool` | 芯片 / MCU 配置修改 |
| `MPPGTool` | 固件下载（烧录），支持 Watch 设备 |
| `CFUDownloadTool` | CFU 固件下载 |
| `DspConfigTool` | DSP 配置 |
| `ImageConverter` | 图像转换 |
| `DebugAnalyzer` | 调试分析 |
| `AciHostCLI` | ACI host 命令行工具 |
| OTA（Android / iOS） | OTA 升级包生成与测试 App |
| AudioConnect（Android / iOS） | 音频连接测试 App |

> 芯片固件包位于 `sdk/bin/`（例如 `sdk/bin/rtl87x3ep/default_bin/`），需使用 `MPPGTool` 下载。

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
├── menu_config.h             # GUI 菜单配置
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

## 更多文档

RTL8773E 系列的数据手册、Quick Start、硬件说明、SDK/GUI 在线文档等官方资料，
参见 [RTL8773E-Series 文档中心](https://www.realmcu.com/zh/Resources/Documentation/RTL8773E-Series#pagetab)。
