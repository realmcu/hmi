# mpcli — MP Tool CLI for Realtek RTL87xx

mpcli 是 Realtek RTL87xx 系列芯片的量产烧录 CLI 工具，支持通过 UART 烧录 flash 镜像。

## 版本 v4.0.0.7 新特性

### 1. `--gen-json` — 基于 MP header 自动生成 flash_image.json

新增 `--gen-json` 子模式，无需连接设备即可从 SDK 构建输出目录生成 `flash_image.json`：

```bash
# 基本用法（不需要 -T）
mpcli --gen-json --bin-dir <SDK_OUTPUT_DIR>
```

工具扫描所有 `.bin` 文件，读取其 **MP header**（由 `prepend_header.exe` 写入），从中提取烧录地址和信息，无需硬编码的 slot 表或文件名模式匹配。

生成的 `flash_image.json` 直接在 SDK 输出目录，随后可通过 `-a` 模式烧录。

### 2. MP header 驱动的工作原理

每个 SDK 输出的 `.bin` 文件开头有 **512 字节 MP header**（TLV 格式），包含：

| ITEM ID | 字段 | 说明 |
|---------|------|------|
| `0x0001` | BIN_ID | 文件类型标识（APP=0x0300, OTA_HEADER=0x0800 等） |
| `0x001C` | **下载地址** | 目标 flash 地址（已区分 BANK0/BANK1） |
| `0x0004` | 数据长度 | 实际内容大小（不含头） |

地址解析顺序：
1. **ITEM 0x001C**：MP header 中的下载地址（优先使用，已含 BANK 信息）
2. **BIN_ID + flash_map.ini**：无 ITEM 0x001C 的文件通过 BIN_ID + 目录路径 + flash_map.ini 解析

### 3. 支持双 Bank（BANK0 + BANK1）

双 Bank 设备的 BANK0 和 BANK1 镜像自动识别到同一份 JSON 中，单 Bank 设备不受影响。

`APP_Config` 等共享文件自动生成两个地址条目（BANK0 + BANK1）。

### 4. JSON 中不再包含 port/baud

`flash_image.json` 不再写入串口参数。端口和波特率在烧录时通过命令行指定：

```bash
mpcli -f flash_image.json -a -c COM3 -b 3000000 -M 5 -r -u -d -T RTL87X3EP
```

兼容性：旧版（含 port/baud 的 JSON）仍可正常解析。

## 完整工作流

### 方式一：一条命令即生成又烧录（推荐）

`--gen-json` 时若**同时**给出 `--bin-dir`、`-T`、`-c`，则生成 `flash_image.json`
后**直接烧录**，一步到位（自动注入默认 `-M 5 -r -b 3000000`，`-b` 可显式覆盖，
`-u`/`-d` 默认已开）：

```bash
# Linux（源码运行）
python3 __main__.py --gen-json --bin-dir <固件目录> -T RTL87X3G -c /dev/ttyUSB0

# Linux（编译版，固件随包在 fw/runtime_system_bin/<芯片>/）
/home/wh/.local/mpcli/mpcli --gen-json \
    --bin-dir /home/wh/.local/mpcli/fw/runtime_system_bin/RTL8783G \
    -T RTL87X3G -c /dev/ttyUSB0

# Windows
mpcli --gen-json --bin-dir "C:\SDK_Build\output" -T RTL87X3EP -c COM3
```

> `-T` 对应关系：RTL8773G / RTL8783G → `RTL87X3G`；RTL87X3EP → `RTL87X3EP`。
> **不给 `-c` 时**（缺 `-c` 或 `-T`）则仅生成 JSON 后退出，不烧录（见方式二第 1 步）。

### 方式二：分两步（生成 / 烧录分离）

```bash
# 1. 生成 flash_image.json（无需接设备）
mpcli --gen-json --bin-dir "C:\SDK_Build\output"

# 2. 烧录全部镜像（需要接设备）
mpcli -f "C:\SDK_Build\output\flash_image.json" -a -c COM3 -b 3000000 -M 5 -r -u -d -T RTL87X3EP

# 3. 若 APP 未在 SDK 目录中，可单独烧录
mpcli -c COM3 -p -A 0x02098000 -F app.bin -b 3000000 -M 5 -r -u -d -T RTL87X3EP
```

## 输出文件

| 文件 | 说明 |
|------|------|
| `flash_image.json` | mpcli JSON 配置文件（-a 模式输入） |
| `flash_image_report.txt` | 烧录报告（镜像列表、大小信息） |

## 命令行参数

| 参数 | 说明 |
|------|------|
| `-T` | IC 类型：RTL87X3E / RTL87X3EP / RTL87X3D / RTL87X3G / RTL87X3J（--gen-json 时不需要） |
| `-c` | COM 端口，如 COM3 |
| `-b` | 修改波特率（烧录时速度） |
| `-B` | 打开波特率（初始连接速度） |
| `-M` | 烧录模式，5 = MP Loader 模式 |
| `-a` | 烧录 JSON 中所有镜像 |
| `-f` | JSON 配置文件路径 |
| `-r` | 烧录完成后重启 |
| `-u` | 解锁保护区域 |
| `-d` | 禁用 OEM 配置合并 |

### --gen-json 专属参数

| 参数 | 说明 |
|------|------|
| `--gen-json` | 启用 JSON 生成模式；若同时给 `--bin-dir`+`-T`+`-c`，生成后直接烧录（一步到位） |
| `--bin-dir` | SDK 构建输出目录（扫描所有 *.bin 文件的 MP header） |
| `--gen-output-dir` | 输出目录（默认 = --bin-dir） |
| `--gen-verbose` | 打印每个文件的 MP header 解析信息 |

> APP 自动发现：若 SDK 目录中存在 `app_*.bin`（符合 MP header 格式），自动集成到 JSON；否则 SKIP，可通过 `-p -A -F` 单独烧录。

## 构建 mpcli.exe

```bash
pip install pyinstaller
pyinstaller mpcli_setup_win.spec
```

生成文件在 `dist/mpcli.exe`。
