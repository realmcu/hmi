# RTL87X3G HMI SDK — Chargebox

本文是 RTL87X3G HMI 多仓库 SDK 的 Chargebox 项目入口。该 SDK 基于 Realtek RTL87X3G SoC 与 Zephyr RTOS，面向带显示、触控和无线连接能力的嵌入式 HMI 应用。

执行本文的 `west init` 和 `west update` 后，会取得 Zephyr、Realtek 平台代码、GUI、Display 驱动、基础镜像、工具及 Chargebox application 等完整 SDK 组件，无需再单独下载 Zephyr 源码。

Chargebox application 当前以 `rtl87x3g_evb` 为目标板，对应 RTL8773GTP SoC。SDK 仅提供 Zephyr 的 `west`、CMake 和 Ninja 构建流程，不提供 Keil MDK 或其他 RTOS 工程。

> **当前版本说明**
>
> Chargebox 工程目前是通用 HMI bring-up 基线：已接入 HoneyGUI、显示、触控、按键和蓝牙相关配置，默认构建 HoneyGUI image widget 示例。客户可在此基础上开发产品界面和业务逻辑；当前代码不包含完整的充电盒产品功能，也不代表已经适配客户量产硬件。

## 功能概览

| 功能 | 当前状态 |
| --- | --- |
| Zephyr RTOS | ✅ 唯一支持的 RTOS 与构建环境 |
| HoneyGUI 显示引擎 | ✅ 已集成，默认启用 image widget 示例 |
| LCD、触控与按键 | ✅ 已按当前 EVB 配置接入 |
| BLE Manager | ✅ 构建配置已启用 |
| BR/EDR（A2DP、AVRCP、HFP、PAN） | ✅ 构建配置已启用 |
| App 串口烧录 | ✅ 支持 `mpcli` |
| J-Link 调试与烧录 | ✅ Zephyr runner 已配置 |
| OTA 双 Bank | ⚠️ 当前 Flash 布局未配置 Bank1 |
| Keil MDK / 非 Zephyr RTOS | ❌ 不支持 |

功能是否适用于最终产品，仍取决于目标 SoC、板级原理图、LCD/Touch 型号、Flash 布局和产品配置。

## 快速开始

### 1. 准备环境

支持 Windows 10/11 x64 和 Ubuntu 22.04 LTS 或更新版本。主要依赖：

- Python ≥ 3.10、CMake ≥ 3.20.5、Ninja、Git 和 west；
- Zephyr SDK toolchain 0.16.9，需要单独安装；
- Windows 构建还需要确保 Git for Windows 的 `bash.exe` 可通过 `PATH` 找到。

Zephyr SDK toolchain 是包含 ARM compiler、assembler 和 linker 的主机构建工具，不是另一份 HMI SDK。详细安装步骤参见[开发环境与 SDK 获取](docs/getting-started_CN.md)。

### 2. 下载 SDK

请选择 GitHub 或 Gitee 中的一种方式初始化 workspace。

**GitHub：**

```bash
mkdir hmi && cd hmi
west init -m https://github.com/realmcu/HMI-MANIFEST.git --mr master --mf rtl8773g-chargebox-github.yml .
west update
```

**Gitee：**

```bash
mkdir hmi && cd hmi
west init -m git@gitee.com:realmcu/hmi-manifest.git --mr master --mf rtl8773g-chargebox-gitee.yml .
west update
```

Gitee 方式需要预先配置 SSH Key。同步完成后安装 Python 依赖：

```bash
west zephyr-export
python -m pip install -r zephyrproject/zephyr/scripts/requirements.txt
```

### 3. 编译固件

```bash
cd zephyrproject/realtek-app/applications/chargebox
west build -b rtl87x3g_evb -p always
```

构建成功后，App 镜像位于 `bin/app.bin`。后续增量构建执行：

```bash
west build -b rtl87x3g_evb
```

> 请进入 Chargebox application 目录构建。若从 workspace 根目录构建，必须显式指定 application 的 build 目录，否则固件后处理会找不到 `mp.ini` 和 `VERSION`。

### 4. 烧录 App

App 单镜像烧录只适用于已经写入匹配基础镜像的开发板。空白芯片或首次整机烧录需要使用当前 SDK release 的完整镜像包。

安装 SDK 中提供的 `mpcli` 后，在 Chargebox application 目录执行：

**Windows PowerShell：**

```powershell
west flash --runner mpcli --port COM6 `
  --file bin/app.bin `
  --bin-address 0x7009E000
```

**Ubuntu：**

```bash
west flash --runner mpcli --port /dev/ttyUSB0 \
  --file bin/app.bin \
  --bin-address 0x7009E000
```

请将串口名称替换为实际设备。烧录工具安装、硬件接线、下载模式和日志参数参见[编译、烧录与日志](docs/build-and-flash_CN.md)。

## 文档导航

| 文档 | 内容 |
| --- | --- |
| [开发环境与 SDK 获取](docs/getting-started_CN.md) | 主机依赖、Python 环境、West 下载和 Zephyr SDK toolchain 安装 |
| [编译、烧录与日志](docs/build-and-flash_CN.md) | 构建命令、产物、完整镜像边界、`mpcli`、硬件连接和串口日志 |
| [SDK 组成与应用移植](docs/workspace-layout_CN.md) | 多仓库结构、组件职责、Chargebox 配置和客户硬件移植入口 |
| [常见问题](docs/troubleshooting_CN.md) | 环境、编译、烧录、启动和串口问题排查 |

## SDK 组件

| 组件 | 本地路径 | 用途 |
| --- | --- | --- |
| RTL87X3G HMI SDK | `zephyrproject/` | Zephyr、Realtek HAL、board、基础镜像与工具 |
| Display | `zephyrproject/modules/display/` | Realtek 显示驱动 |
| HoneyGUI | `zephyrproject/modules/honeygui/` | HMI GUI 引擎、控件和示例 |
| LVGL | `zephyrproject/modules/lvgl/` | LVGL GUI 模块 |
| Chargebox | `zephyrproject/realtek-app/applications/chargebox/` | Chargebox application 与板级配置 |

组件 revision 由 West manifest 统一管理。请通过 manifest 获取匹配的 SDK 组合，不要独立替换其中某个组件。

## 重要限制

- 当前 board 为 `rtl87x3g_evb`，对应 RTL8773GTP，不代表已适配所有 RTL87X3G SoC 或客户量产板。
- 默认界面是 HoneyGUI image widget 示例，不是完整 Chargebox 产品 UI。
- 当前 Flash 布局未配置 OTA Bank1。
- `bin/app.bin` 只能更新 App 分区，不能代替空白芯片所需的完整烧录包。
- 编译成功不能代替真实硬件上的显示、触控、无线和产品功能验证。

## 参考资料

- [Zephyr 3.7 Getting Started Guide](https://docs.zephyrproject.org/3.7.0/develop/getting_started/index.html)
- [Zephyr West 文档](https://docs.zephyrproject.org/3.7.0/develop/west/index.html)
- [HoneyGUI 文档](https://docs.honeygui.com/)
