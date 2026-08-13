# 开发环境与 SDK 获取

本文说明如何准备 RTL87X3G HMI SDK 的开发环境、通过 West 获取完整多仓库源码，以及安装编译所需的 Zephyr SDK toolchain。

> [返回项目首页](../README_CN.md)

## 1. SDK 与 toolchain 的区别

RTL87X3G HMI SDK 是由 West 管理的完整多仓库 SDK。执行 `west update` 后，workspace 已经包含：

- Zephyr 3.7.2 源码；
- Realtek RTL87X3G 平台代码和 HAL；
- Display、HoneyGUI 和 LVGL；
- Chargebox application；
- 平台基础镜像和烧录工具。

因此，无需再单独下载 Zephyr 源码。

Zephyr SDK toolchain 0.16.9 是安装在开发主机上的外部构建工具，其中包含 ARM GCC compiler、assembler、linker 等组件。它不是另一份 HMI SDK，也不包含 Chargebox application，需要单独下载安装。

## 2. 支持的主机环境

推荐使用：

- Windows 10/11 x64；
- Ubuntu 22.04 LTS 或更新版本。

Windows 原生环境最适合同时完成编译、串口烧录和日志查看。WSL 可用于编译，但 USB/COM 设备访问更复杂，不建议初次使用 SDK 时采用 WSL。

## 3. 工具依赖

| 工具 | 版本要求 | 检查命令 | 说明 |
| --- | ---: | --- | --- |
| Git | 建议使用最新稳定版 | `git --version` | 下载 West workspace；Windows 版本还提供固件后处理所需的 Bash |
| Python | ≥ 3.10 | `python --version` | Windows 推荐 Python 3.10 或 3.11 |
| CMake | ≥ 3.20.5 | `cmake --version` | Zephyr 构建系统 |
| Ninja | 建议使用最新稳定版 | `ninja --version` | 默认构建后端 |
| Devicetree Compiler | ≥ 1.4.6 | `dtc --version` | Devicetree 编译工具 |
| west | ≥ 0.14.0 | `west --version` | Workspace 与构建命令 |
| Zephyr SDK toolchain | 0.16.9 | — | 需单独安装；Zephyr 源码已由 `west update` 获取 |

下载入口：

- [Python](https://www.python.org/downloads/)
- [Git](https://git-scm.com/downloads)
- [CMake](https://cmake.org/download/)
- [Zephyr SDK Releases](https://github.com/zephyrproject-rtos/sdk-ng/releases)

> Windows 下请确保 Git for Windows 的 `bash.exe` 可通过 `PATH` 找到。固件后处理需要 Bash，即使主体构建是在 PowerShell 或 `cmd.exe` 中执行。

## 4. 创建 Python 虚拟环境

建议将 Python 依赖安装到 workspace 内的虚拟环境，避免与系统中的其他 SDK 冲突。

### Windows PowerShell

```powershell
mkdir hmi
cd hmi
python -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install --upgrade pip west
```

### Ubuntu

```bash
mkdir hmi
cd hmi
python3 -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip west
```

以后打开新终端时，需要先重新激活虚拟环境。

## 5. 下载完整 SDK

请选择 GitHub 或 Gitee 中的一种方式初始化。West 会根据 manifest 同步全部组件仓库，无需逐个执行 `git clone`。

### 5.1 GitHub

```bash
west init -m https://github.com/realmcu/HMI-MANIFEST.git \
  --mr master \
  --mf rtl8773g-chargebox-github.yml \
  .
west update
```

### 5.2 Gitee

Gitee manifest 中的组件仓库使用 SSH 地址，执行前请先在 Gitee 配置 SSH Key。

```bash
west init -m git@gitee.com:realmcu/hmi-manifest.git \
  --mr master \
  --mf rtl8773g-chargebox-gitee.yml \
  .
west update
```

> Windows PowerShell 不支持 Bash 的反斜杠续行语法。请将 `west init` 写成一行执行，或使用 PowerShell 的反引号续行。

同步完成后，导出 Zephyr CMake package 并安装当前 Zephyr 版本的 Python 依赖：

```bash
west zephyr-export
python -m pip install -r zephyrproject/zephyr/scripts/requirements.txt
```

常用 workspace 命令：

```bash
west topdir             # 显示 workspace 根目录
west list               # 显示所有受 West 管理的仓库
west update             # 按 manifest 同步所有仓库
west manifest --resolve # 查看解析后的 manifest
```

## 6. 安装 Zephyr SDK toolchain

下载并解压 Zephyr SDK toolchain 0.16.9，然后执行其中的安装脚本。

### Windows

```bat
cd <zephyr-sdk-0.16.9>
setup.cmd
```

### Ubuntu

```bash
cd <zephyr-sdk-0.16.9>
./setup.sh
```

安装脚本通常只需执行一次。移动 SDK 目录后需要重新执行。CMake 一般可以自动找到已注册的 Zephyr SDK；系统中安装了多个版本时，可显式指定：

### Windows PowerShell

```powershell
$env:ZEPHYR_TOOLCHAIN_VARIANT = "zephyr"
$env:ZEPHYR_SDK_INSTALL_DIR = "C:\path\to\zephyr-sdk-0.16.9"
```

### Ubuntu

```bash
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
export ZEPHYR_SDK_INSTALL_DIR=$HOME/zephyr-sdk-0.16.9
```

环境准备完成后，继续阅读[编译、烧录与日志](build-and-flash_CN.md)。
