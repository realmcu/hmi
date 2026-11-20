# eBadge（RTL8773G + RTL8711）

Zephyr 应用工程：HoneyGUI 界面 + OTA 双 bank 固件 + FlashDB 预制壁纸，板级目标
`rtl87x3g_evb`，通过 SPI 主机模式外挂 RTL8711 Wi-Fi 模组。

- 编译产物：`bin/app.bin`、`bin/bank0/app_bank0.bin`、`bin/bank1/app_bank1.bin`
- 合并资源后的镜像：`bin/bank<N>/app_bank<N>_packed.bin`
- 单烧用的 romfs：`bin/bank<N>/app_romfs_bank<N>_reloc.bin`
- FlashDB 预制资源：`tools/pack/fdb_resource_bin/fdb_prebuilt.bin`

## 目录

| 路径 | 说明 |
|---|---|
| [tools/scripts/](tools/scripts/) | 编译 / 打包 / 烧录脚本（`.sh` = WSL/Linux，`.ps1` = Windows，功能一一对应） |
| [tools/pack/](tools/pack/) | 打包工具 `pack.py`（app+romfs 合并、FDB 生成、布局校验）与 `layout.json`；`fdb_resource_bin/` 是 FDB 素材与产物 |
| [snippets/](snippets/) | 本工程独占的 snippet：`wifi`、`flash_32M_bank0/1` |
| [app/designer/](app/designer/) | HoneyGUI Designer 工程（UI、素材、生成的 C 代码） |
| [port/](port/) | 板级/中间件适配（display、flashdb、GUI port 等） |
| [component/](component/)、[boards/](boards/)、[dts/](dts/) | 组件、板级定义与设备树 |
| `scripts/` | 独立仓库（HoneyRepo/hmi），提供 `scripts/tool/mpcli/mpcli.exe` 等工具，**不随本仓库管理** |

## 脚本一览

两套脚本按运行环境区分，**功能一一对应**：WSL/Linux 用 `.sh`，Windows 用 `.ps1`。
不要用 PowerShell 跑 `.sh`，也不要在 WSL 里跑 `.ps1`。
所有脚本都从自身位置推导仓库根目录，**可在任意目录执行**，输出/中间目录仍在仓库根下。

| 作用 | WSL / Linux | Windows PowerShell |
|---|---|---|
| 编译（可选打包 / 烧录） | `./tools/scripts/build.sh` | `.\tools\scripts\build.ps1` |
| 生成预制壁纸 FDB 镜像 | `./tools/scripts/build_fdb.sh` | `.\tools\scripts\build_fdb.ps1` |
| 只烧 app 镜像 | `./tools/scripts/flash_app.sh` | `.\tools\scripts\flash_app.ps1` |
| 烧 FDB 镜像 | `./tools/scripts/flash_fdb.sh` | `.\tools\scripts\flash_fdb.ps1` |
| 只烧 romfs | `./tools/scripts/flash_romfs.sh` | `.\tools\scripts\flash_romfs.ps1` |
| 布局校验 | `python3 tools/pack/pack.py check` | `python tools\pack\pack.py check` |

`pack.py` 是两套脚本共用的打包工具（check / app / fdb），脚本都只是它的薄封装。
`.sh` 用 `-h/--help` 查看参数；`.ps1` 用 `Get-Help .\tools\scripts\build.ps1 -Detailed`。

## 环境准备

**WSL/Linux**

- west 跑在装有 Zephyr 依赖的 venv 中，默认 `/home/sheldeng/Mi_eBadge/.venv/bin/python`
- Zephyr SDK，默认 `/home/sheldeng/zephyr-sdk-0.16.9`
- Linux 版 mpcli：`realtek-app/tools/mpcli/Linux/mpcli`（`build.sh --flash` 使用）
- 烧 Windows COM 口时需要 WSLInterop 或 `/init` 回退（flash 脚本已自动处理）

**Windows**

- Python 3.12（`build.ps1` 用它跑 west 和 `pack.py`）
- Zephyr SDK（Windows 版，含 `arm-zephyr-eabi-gcc.exe`）。`build.ps1` 在
  `EBADGE_SDK_DIR` / `ZEPHYR_SDK_INSTALL_DIR` 都没设时，会依次试
  `C:\zephyr-sdk-0.16.9`、`C:\zephyr-sdk-0.16.8`、`D:\zephyr-sdk-0.16.9`、
  `D:\zephyr-sdk-0.16.8`；都不中就报错让你设环境变量。
- **CMake 和 Ninja**（`west build` 的硬性依赖）。装好后要重开 PowerShell 让 PATH 生效，
  `build.ps1` 会先检查 `cmake` 是否可见。
- **Git Bash（`bash.exe`）**：Realtek 的 cmake 用它跑 `version_gen_config.sh`
  （生成 VERSION）和 `md5_generate.sh`（post-build）。Windows 自带的
  `%LOCALAPPDATA%\Microsoft\WindowsApps\bash.exe` 是 WSL 启动器，不认 `C:/...`
  这种 Windows 路径，会报 `No such file or directory`（exit 127）—— 轻则 VERSION
  退化成 `0.0.0`，重则最后 md5 步骤让整次编译失败。`build.ps1` 发现 PATH 里是
  WSL 那个时会自动把 Git Bash 的 `bin` 顶到最前。
- Windows 版 mpcli：`scripts\tool\mpcli\mpcli.exe`
- **MinGW-w64 gcc 或 clang**：仅 `build_fdb.ps1` 需要（在 PC 上编译 FlashDB
  生成 FDB 镜像）。放在 PATH 即可，或用 `CC` 指定编译器路径。

路径都可覆盖，换机器不必改脚本：

| 变量 | 脚本 | 说明 |
|---|---|---|
| `VENV_PYTHON` / `SDK_DIR` / `BOARD` / `MPCLI` | build.sh | venv python、SDK、板名、mpcli 路径 |
| `EBADGE_PYTHON` / `EBADGE_SDK_DIR` / `EBADGE_BOARD` / `EBADGE_MPCLI` / `EBADGE_PORT` | build.ps1 | 同上，PowerShell 版 |
| `EBADGE_ROMFS` / `EBADGE_ROMFS_OFFSET` | build.ps1 | `-Pack` 的 romfs 输入、固定偏移 |
| `ASSETS_DIR` / `OUT` / `WITH_VIDEO` | build_fdb.sh / build_fdb.ps1 | 素材目录、输出文件、是否带视频 |
| `CC` | build_fdb.sh / build_fdb.ps1 | host C 编译器（Windows 默认自动找 gcc/clang） |
| `MPCLI_EXE` / `PORT` / `BAUD` / `DRY` | flash_*.sh | mpcli 路径、串口、波特率、只打印命令 |
| `FW` / `ADDR` | flash_app.sh | 镜像文件、烧录地址（默认按 `--bank` 推导） |
| `LAYOUT` / `FW` / `CHUNK_SIZE` | flash_fdb.sh | 布局文件、镜像、分段大小 |

## 编译

```bash
# 默认：OTA bank0 + wifi，增量编译 -> bin/bank0/app_bank0.bin
./tools/scripts/build.sh

# 干净重建（首次 / 新增 .c 文件后必须）
./tools/scripts/build.sh --pristine

# bank1
./tools/scripts/build.sh --bank 1

# 两个 bank 依次编译
./tools/scripts/build.sh --bank both --pristine

# 不带 bank/snippet 的普通开发编译 -> bin/app.bin
./tools/scripts/build.sh --bank '' --snippet ''
```

Windows：

```powershell
.\tools\scripts\build.ps1 -Bank 0 -Snippet wifi -Pristine
.\tools\scripts\build.ps1 -Bank both -Pristine
```

> **新增 `.c` 文件后一定要 pristine。** `app/spi_at_master/CMakeLists.txt` 用
> `file(GLOB)`，增量构建看不到新文件，症状是链接期未定义符号。

构建目录按 snippet 组合区分（`build_wifi/`、`build_bank0_wifi/`、
`build_bank1_wifi/`……），切换组合不会触发 Zephyr 的 snippet 变化检查。

## 打包（app + romfs）

```bash
# WSL / Linux
./tools/scripts/build.sh --bank 0 --pack
./tools/scripts/build.sh --bank both --pack --romfs /path/to/app_romfs.bin
```

```powershell
# Windows
.\tools\scripts\build.ps1 -Bank 0 -Pack
.\tools\scripts\build.ps1 -Bank both -Pack -NoMp
.\tools\scripts\build.ps1 -Bank 0 -Pack -Romfs D:\assets\app_romfs.bin
```

`-Pack` / `--pack` 会把 romfs 按 4KB 对齐追加到 app 后，并迭代到 `ROMFS_OFFSET` 收敛：

- 产物：`bin/bank<N>/app_bank<N>_packed.bin`（完整镜像）
- 附带：`bin/bank<N>/app_romfs_bank<N>_reloc.bin`（已重定位，可单独烧）
- 不追加 MP 头：`-NoMp` / `--no-mp`

细节见 [tools/pack/README.md](tools/pack/README.md)。

## Flashing

**整镜像（含 romfs）** —— 打包后直接烧：

```bash
# WSL / Linux：Linux 版 mpcli，走 /dev/ttyUSBx（先 usbipd attach）
./tools/scripts/build.sh --bank 0 --flash --port /dev/ttyUSB0
```

```powershell
# Windows：COM 口
.\tools\scripts\build.ps1 -Bank 0 -Pack -Flash -Port COM7
```

烧录地址取自 `flash_map.ini`（当前 bank0 = `0x7009E000`，bank1 = `0x704F3000`）。

**只烧 app**（不动 romfs / FDB）：

```bash
# WSL：经由 Windows 版 mpcli.exe
./tools/scripts/flash_app.sh --bank 0            # 默认 COM5
./tools/scripts/flash_app.sh COM7 --bank 1
./tools/scripts/flash_app.sh --fw bin/bank0/app_bank0_packed.bin
DRY=1 ./tools/scripts/flash_app.sh               # 只打印命令
```

```powershell
# Windows
.\tools\scripts\flash_app.ps1                    # bank0 + COM5
.\tools\scripts\flash_app.ps1 -Bank 1 -Port COM7
.\tools\scripts\flash_app.ps1 -Fw bin\bank0\app_bank0_packed.bin -DryRun
```

**只更新 UI 素材**（改壁纸、字号等后不用重烧 app）：

```bash
# WSL / Linux
./tools/scripts/build.sh --bank 0 --pack      # 先生成 _reloc.bin
./tools/scripts/flash_romfs.sh --bank 0       # 默认 COM5
DRY=1 ./tools/scripts/flash_romfs.sh          # 只打印命令
```

```powershell
# Windows
.\tools\scripts\build.ps1 -Bank 0 -Pack
.\tools\scripts\flash_romfs.ps1 -Bank 0       # 默认 COM5
.\tools\scripts\flash_romfs.ps1 -DryRun
```

脚本会校验 romfs 是否 4KB 对齐、目录项指针是否指向本次挂载地址，不通过就拒绝烧。
**不要**直接烧 `app/designer/build/app_romfs_*.bin`，那是旧绝对地址。

**预制壁纸（FlashDB）**：

```bash
# WSL / Linux
./tools/scripts/build_fdb.sh                  # -> tools/pack/fdb_resource_bin/fdb_prebuilt.bin
./tools/scripts/flash_fdb.sh COM5             # 烧到 layout.json 的 fdb.kvdb_addr
```

```powershell
# Windows（生成 FDB 需要 MinGW gcc/clang，见“环境准备”）
.\tools\scripts\build_fdb.ps1
.\tools\scripts\flash_fdb.ps1                 # 默认 COM5
.\tools\scripts\flash_fdb.ps1 -DryRun
```

素材与产物默认都在 `tools/pack/fdb_resource_bin/`（随工程管理）；该目录没有
`wp-1.bin` 时退回 designer 构建树。可用 `--assets-dir` / `-AssetsDir`、
`--out` / `-Out` 或 `ASSETS_DIR` / `OUT` 指定其它位置。
烧录前会核对镜像大小、KVDB 地址，并检查是否覆盖 app/OTA 分区。

> WSL 下 COM 口属于 Windows，必须用 `mpcli.exe`。`flash_*.sh` 会先尝试直接执行
> `.exe`（WSLInterop 已注册时），否则退回 `/init` + 临时 `.bat`。
> Windows 下 `flash_*.ps1` 则直接调用 `mpcli.exe`，无需这些处理。

## 布局校验

改动 flash 规划后：

```bash
python3 tools/pack/pack.py check
```

把 [tools/pack/layout.json](tools/pack/layout.json) 与 `flash_map.h`、分区表 overlay、
`port/flashdb/fal_cfg.h`、romfs 挂载代码逐一比对，只报告不一致。

## 常用排错

| 现象 | 原因 / 处理 |
|---|---|
| 链接期未定义符号 | 新增 `.c` 后未 pristine |
| 分区表被叠加两次 | `realtek-app/snippets/` 下也有同名 `flash_32M_bank*`，删掉那份 |
| UI 全黑、目录项乱码 | romfs 落点与偏移不匹配，重跑 `-Pack` / `--pack`；或烧了非 `_reloc.bin` |
| `already mounted` | 同一 prefix 重复 `gui_vfs_mount_romfs()`，换个 prefix |
| 串口打不开 | WSL：先 `usbipd attach`；Windows：确认 COM 号未被占用 |
| `mpcli` 找不到 fw | 必须从 mpcli 所在目录运行（脚本已处理） |
| PowerShell 报语法错误 / 中文乱码 | `.ps1` 必须是 **UTF-8 with BOM**（仓库里的文件已带），不要另存为 ANSI/GBK |
| 改了 `romfs_layout.h` 固件仍挂旧地址 | 增量构建不会重编入口文件；`-Pack`/`--pack` 现在用 `pack.py fwcheck` 检测并自动 touch 重编，手动构建可自行 touch `Mi_eBadgeEntry.c` |
| `-Pack` 只编译不产出 `_packed.bin` | 看构建阶段是否失败；Designer 重新导出会把 `romfs_layout.h` 从 `src/` 挪到 `src/user/`，脚本已兼容两处，旧编译产物建议 `-Pristine` 一次 |
| `-Pack` 反复 `rebuild bankN (4)/(5)`，最后「6 轮仍未收敛」 | `build_bank<N>` 和 `build_bank<N>_wifi` 同时在时，`pack.py fwcheck` 会挑到没在用的那个 ELF，于是永远判 stale。已修：默认取 `bin/<bank>/app_<bank>.elf`（与镜像同一次产出），`build.ps1` / `build.sh` 也显式传 `--elf` |
| 带 wifi 和不带 wifi 交替编译后 `-Pack`，romfs 压进 app 尾部 | `bin/bank<N>/` 和 `romfs_layout.h` 各 snippet 组合**共用**，而两种 app 尺寸差约 34 KB。build 和 pack 必须用同一个 `-Snippet` 组合；换组合后不要只 build 不 pack |
| `romfs 会压到 app 代码上，拒绝打包`（`pack.py` 硬校验） | 说明 `app_bank<N>.bin` 和固件 ELF 不是同一次编译 —— 偏移是按小的那份算的。别手改 `romfs_layout.h`，按报错里给的命令用**你实际要烧的那套参数**重跑 `-Pack` / `--pack`，它会把偏移重算、touch 入口文件重编、再打包，1~2 轮收敛 |
| `build_fdb.ps1` 找不到 host C 编译器 | 安装 MinGW-w64 或 clang 并加入 PATH，或设置 `CC=<gcc.exe 路径>` |
| `找不到 Zephyr SDK 工具链` | 设 `EBADGE_SDK_DIR`（或 `ZEPHYR_SDK_INSTALL_DIR`）指向 SDK 根目录 |
| `PATH 里没有 cmake：west build 需要 CMake` | 装 CMake + Ninja，重开 PowerShell（不是脚本的问题） |
| `version_gen_config.sh: No such file or directory`、`exit 127`、产出的 `_MP.bin` 版本号是 `0.0.0` | PATH 里的 `bash.exe` 是 WSL 启动器。`build.ps1` 会把 Git Bash 顶到 PATH 最前，但 CMake 把 `find_program` 结果缓存在 `CMakeCache.txt` 里，**老 build 目录要 `-Pristine` 一次**才会重新探测（脚本会在检测到残留时警告） |
| `.ps1` 一旦 `> log.txt` / `2>&1 \| Tee-Object` 就报 `NativeCommandError` 中止 | PowerShell 5.1 会把子进程 stderr 包成错误，配上 `$ErrorActionPreference='Stop'` 就变成终止错误。`build_fdb.ps1` / `flash_*.ps1` 已改用 `_common.ps1` 的 `Invoke-Native` 规避（其余脚本请照此办理，不要再直接 `& exe`） |
