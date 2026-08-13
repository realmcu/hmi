# SDK 组成与应用移植

本文介绍 RTL87X3G HMI 多仓库 SDK 的 workspace 组成、组件职责，以及 Chargebox application 的主要配置和客户硬件移植入口。

> [返回项目首页](../README_CN.md)

## 1. Workspace 组成

通过公开 manifest 同步后的主要目录如下：

```text
hmi/
├── .west/                                      # West workspace 元数据
├── .manifest/                                  # HMI manifest 仓库
└── zephyrproject/
    ├── zephyr/                                 # Realtek Zephyr 3.7 基线与 rtl87x3g_evb board
    ├── modules/
    │   ├── hal/realtek/                        # RTL87X3G HAL 与 SoC 支持
    │   ├── display/                            # Realtek Display 驱动
    │   ├── honeygui/                           # HoneyGUI 引擎与示例
    │   ├── lvgl/                               # LVGL 模块
    │   └── ...                                 # Zephyr 依赖模块
    └── realtek-app/
        ├── applications/chargebox/             # Chargebox application
        ├── bin/                                # 平台基础镜像与 trace 文件
        ├── cmake/                              # Realtek application 构建扩展
        └── tools/mpcli/                        # Windows/Linux 串口烧录工具
```

## 2. 组件仓库

| 组件 | 本地路径 | 用途 |
| --- | --- | --- |
| [rtl87x3g-hmi-sdk](https://github.com/realmcu/rtl87x3g-hmi-sdk) | `zephyrproject/` | Zephyr、Realtek HAL、board、基础镜像与工具 |
| [display](https://github.com/realmcu/display) | `zephyrproject/modules/display/` | LCDC、PPE、IDU、JPU 等显示驱动 |
| [HoneyGUI](https://github.com/realmcu/HoneyGUI) | `zephyrproject/modules/honeygui/` | HMI GUI 引擎、控件和示例 |
| [LVGL](https://github.com/realmcu/lvgl) | `zephyrproject/modules/lvgl/` | LVGL GUI 模块 |
| [hmi](https://github.com/realmcu/hmi/tree/rtl8773g-chargebox) | `zephyrproject/realtek-app/applications/chargebox/` | Chargebox application 与板级配置 |

公开 manifest 将 Zephyr、Realtek HAL 和 `realtek-app` 等内容统一交付在 `rtl87x3g-hmi-sdk` 仓库中，同时通过 West 挂载 Display、HoneyGUI、LVGL 和 Chargebox application。客户应通过 manifest 获取匹配的组件组合，不要独立替换其中某个仓库的 revision。

## 3. Chargebox application 结构

以下路径相对于 `zephyrproject/realtek-app/applications/chargebox/`：

```text
chargebox/
├── boards/
│   └── rtl87x3g_evb.overlay     # Application 级 Devicetree overlay
├── port/
│   └── ui/                      # HoneyGUI 的显示、输入、OS、文件系统和 FTL 适配
├── CMakeLists.txt               # Zephyr application 构建入口
├── Kconfig                      # Application 自定义 Kconfig
├── prj.conf                     # 默认 Zephyr 功能配置
├── main.c                       # Application 入口
├── app_lower_init.c/.h          # 平台底层初始化
├── flash_map.h                  # 当前产品 Flash 分区布局
├── mp.ini                       # App 镜像 header/MP 配置
└── VERSION                      # App 版本与 GCID
```

## 4. 默认 GUI 示例

`prj.conf` 当前启用：

```text
CONFIG_REALTEK_HONEYGUI=y
CONFIG_REALTEK_BUILD_EXAMPLE_IMAGE_WIDGET=y
```

对应代码来自 HoneyGUI module 中的 image widget 示例。该示例用于验证 GUI、显示和平台适配链路，不是完整的 Chargebox 产品界面。

开发产品 UI 时，应按项目需求切换 HoneyGUI 示例或接入产品 UI，并同步评估资源、内存和 Flash 分区。

## 5. 客户硬件移植入口

面向客户硬件移植时，通常至少需要检查：

| 文件或目录 | 检查内容 |
| --- | --- |
| `boards/rtl87x3g_evb.overlay` | 设备节点、引脚、外设实例及状态 |
| `prj.conf` | LCD、Touch、按键、GUI、蓝牙和其他 Zephyr 配置 |
| `port/ui/` | HoneyGUI 的显示、输入、文件系统、FTL 和 OS 适配 |
| `flash_map.h` | App、User Data、FTL 和 OTA 分区 |
| `mp.ini` | 固件 header 与烧录相关配置 |
| `VERSION` | Application 版本与 GCID |

### 5.1 SoC 与 board

当前 Chargebox application 使用 `rtl87x3g_evb`，该 board 对应 RTL8773GTP。将 application 移植到其他 RTL87X3G SoC 或客户板时，不能只修改 board 名称；还需要核查 SoC、DTS、引脚、内存、外部 Flash、基础镜像、Display、Touch、OTA 和烧录配置。

### 5.2 显示与输入设备

当前配置使用特定 LCD、Touch 和按键驱动。客户硬件采用不同器件时，需要同步调整 Devicetree、Kconfig 和相关驱动配置，并在真实硬件上验证：

- 分辨率和像素格式；
- LCD 接口和时序；
- Touch 坐标方向与校准；
- Framebuffer 和 GUI 内存；
- 背光、复位、电源和中断引脚。

### 5.3 Flash 布局

修改 `flash_map.h` 前，需要同时核对：

- 基础镜像使用的布局；
- application 链接和加载地址；
- `mpcli` 烧录地址；
- App 与 User Data 的最大尺寸；
- FTL 和 OTA 分区；
- 实际 Flash 容量。

必须避免分区重叠，也不能混用不同 SDK release 或不同硬件配置生成的基础镜像。

### 5.4 验证边界

编译成功只能证明源码与构建配置能够生成固件，不能证明目标硬件上的显示、触控、无线功能或产品逻辑正确。完成板级移植后，应至少在目标硬件上验证启动、显示、输入、串口日志和烧录流程。
