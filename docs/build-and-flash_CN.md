# 编译、烧录与日志

本文说明 Chargebox application 的构建流程、固件产物、App 镜像烧录方式和串口日志参数。开始前请先完成[开发环境与 SDK 获取](getting-started_CN.md)。

> [返回项目首页](../README_CN.md)

## 1. 编译固件

### 1.1 首次编译

请进入 Chargebox application 目录后再执行构建。这样可以确保 `build/`、`bin/`、`mp.ini` 和 `VERSION` 使用正确的相对位置。

```bash
cd zephyrproject/realtek-app/applications/chargebox
west build -b rtl87x3g_evb -p always
```

后续未修改 board 或关键 Kconfig 时，可执行增量编译：

```bash
west build -b rtl87x3g_evb
```

也可以从 workspace 根目录显式指定 source 和 build 目录：

```bash
west build \
  -b rtl87x3g_evb \
  -d zephyrproject/realtek-app/applications/chargebox/build \
  zephyrproject/realtek-app/applications/chargebox
```

> 不要从 workspace 根目录省略 `-d`。默认生成的根目录 `build/` 会使固件后处理阶段找不到 application 目录中的 `mp.ini` 和 `VERSION`。

### 1.2 构建产物

构建成功后，主要产物位于 `applications/chargebox/bin/`：

| 文件 | 用途 |
| --- | --- |
| `app.bin` | 已完成 Realtek App header 处理的 App 烧录镜像 |
| `app_MP-*.bin` | 带版本、GCID 和 MD5 文件名的发布镜像 |
| `app.elf` | 调试和符号分析 |
| `app.map` | 链接映射 |
| `app.lst` | 反汇编列表 |
| `app.trace` | Realtek log trace |
| `app.config` | 本次构建的完整 Kconfig 配置 |
| `app.dts` | 本次构建生成的 Devicetree |

Zephyr 原始产物位于 `applications/chargebox/build/zephyr/`。使用 `mpcli` 更新 App 时，应烧录 `bin/app.bin`，不要使用未经 App header 后处理的 `build/zephyr/zephyr.bin`。

### 1.3 常用构建命令

以下命令均在 `applications/chargebox/` 中执行：

| 命令 | 说明 |
| --- | --- |
| `west build -b rtl87x3g_evb` | 增量构建 |
| `west build -b rtl87x3g_evb -p always` | 清理后完整构建 |
| `west build -t menuconfig` | 打开 Kconfig 配置界面 |
| `west build -t guiconfig` | 打开图形化 Kconfig 配置界面 |
| `west build -t rom_report` | 查看 ROM/Flash 使用明细 |
| `west build -t ram_report` | 查看 RAM 使用明细 |
| `west build -t pristine` | 删除当前 build 目录中的全部构建结果 |

## 2. 烧录固件

### 2.1 App 单镜像与完整烧录

本节的 App 单镜像烧录适用于已经写入兼容 Boot Patch、Upperstack、System Patch、DSP 和配置镜像的开发板。

空白芯片或首次整机烧录必须使用对应 SDK release 提供的完整镜像包。不能只写入 `app.bin`，也不要混用不同 SDK release 的基础镜像。

Chargebox 当前 App 分区为：

```text
Bank0 App address: 0x7009E000
Bank0 App size:    0x00180000 (1536 KiB)
```

烧录地址和分区容量以本工程的 `flash_map.h` 为准。

### 2.2 安装 mpcli

West 已同步 Windows 和 Linux 版本的 `mpcli`。安装脚本会将当前平台的工具目录加入用户 `PATH`。

**Windows PowerShell，在 workspace 根目录执行：**

```powershell
powershell -ExecutionPolicy Bypass -File zephyrproject\realtek-app\tools\mpcli\setup.ps1
mpcli --help
```

**Ubuntu，在 workspace 根目录执行：**

```bash
source zephyrproject/realtek-app/tools/mpcli/setup.sh
mpcli --help
```

`mpcli` 同目录下的 `fw/` 和 `config/` 是工具运行所需文件，请勿单独移动或删除。

### 2.3 硬件连接与下载模式

使用 USB-to-UART 适配器连接下载串口：

| RTL87X3G Pin | 信号 | USB-to-UART |
| --- | --- | --- |
| P3_0 | RXD | TX |
| P3_1 | TXD | RX |
| GND | GND | GND |

进入 MP 下载模式：

1. 将 `P2_0` 拉到 GND；
2. 复位或重新上电设备；
3. 开始烧录；
4. 烧录完成并复位后，根据硬件设计释放 `P2_0`。

### 2.4 使用 West + mpcli 烧录 App

先完成编译，然后在 `applications/chargebox/` 目录执行。

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

请将 `COM6` 或 `/dev/ttyUSB0` 替换为实际下载串口。

> `rtl87x3g_evb` 的默认 `west flash` runner 是 J-Link。使用串口下载时必须显式指定 `--runner mpcli`，否则直接执行 `west flash` 会尝试调用 J-Link。

### 2.5 直接调用 mpcli

也可以直接烧录单个 App 镜像。`<absolute-path-to-app.bin>` 建议使用绝对路径。

**Windows PowerShell：**

```powershell
mpcli -c COM6 -T RTL87X3G -M 5 `
  -p -A 0x7009E000 `
  -F <absolute-path-to-app.bin> `
  -b 3000000 -r
```

**Ubuntu：**

```bash
mpcli -c /dev/ttyUSB0 -T RTL87X3G -M 5 \
  -p -A 0x7009E000 \
  -F <absolute-path-to-app.bin> \
  -b 3000000 -r
```

成功时工具会输出类似：

```text
Download : Success | 1 image files have been downloaded successfully!
MPCLI exit: success.
```

详细参数说明参见 SDK 中与当前平台对应的 `realtek-app/tools/mpcli/Windows/` 或 `realtek-app/tools/mpcli/Linux/` 使用文档。

## 3. 查看串口日志

使用串口终端连接开发板的日志串口：

| 参数 | 配置 |
| --- | --- |
| 波特率 | `2000000` |
| 数据位 | `8` |
| 停止位 | `1` |
| 校验位 | `None` |
| 流控 | `None` |

请在串口终端中关闭 RTS/CTS 和 DTR/DSR 硬件流控，避免控制线影响设备复位。具体串口号可在 Windows 设备管理器或 Linux 的 `/dev/ttyUSB*` 设备中确认。

> 烧录口与日志口是同一个串口时，烧录前必须关闭串口终端，释放串口设备。

遇到问题时，参见[常见问题](troubleshooting_CN.md)。
