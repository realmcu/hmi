# scripts

本目录下的常用工具脚本，覆盖 RTL87X3G 应用的**编译 → 下载 → 抓 log** 完整链路。以下命令默认在 `applications/` 目录执行，因此写作 `scripts/...`；若从 west 工作区根目录执行，请加上 `applications/` 前缀。脚本内部路径按自身位置解析，不依赖调用时的当前目录。

- `build.sh` —— `west build` 封装
- `flash-linux-app.sh` —— 原生 Linux 下用 Linux 版 `mpcli` 烧录 App
- `flash-linux2win-app.sh` —— Linux 工作电脑通过 SSH 驱动连接开发板的 Windows 电脑烧录 App
- `flash-wsl-app.sh` —— WSL2 里驱动 Windows 版 `mpcli.exe` 烧录 App
- `flash-win32-app.bat` —— Windows 原生 cmd 下直接调用 `mpcli.exe` 烧录 App
- `flash-macos-app.sh` —— **macOS** 下用 macOS 版 `mpcli` 烧录 App（串口自动探测 `cu.*`），见「macOS 原生」
- `flash-runtime-linux.sh` / `flash-runtime-wsl.sh` / `flash-runtime-win32.bat` / `flash-macos-runtime.sh` —— 烧录**整套 runtime**（boot patch / upperstack / stack & sys patch / DSP / Config，共 11 个镜像），见「2.5 烧录 runtime」
- `flash-linux2win-runtime.sh` —— Linux 工作电脑通过 SSH 驱动 Windows 电脑烧录 runtime
- `flash-wsl-romfs.sh` / `flash-linux2win-romfs.sh` / `flash-macos-romfs.sh` —— 烧录 HoneyGUI romfs 资源镜像，见「2.6 烧录 romfs」
- `tool/mpcli/` —— 随仓库自带的 mpcli v4.0.0.7；`mpcli`（Linux ELF）与 `mpcli.exe`（Windows）并排放，`fw/` 和 `config/` 共用。所有 flash 脚本默认从这里取，不再依赖外部安装。macOS 版 `mpcli-macos`（Mach-O，构建产物不入库）也放这个目录，脚本默认就取它，行为与其它平台脚本一致
- `serial_term.py` —— **Windows 专用**串口终端（依赖 `msvcrt`，Linux 下跑不了）。Linux 下看 log 见「查看 LOG」章节，用现成命令即可
- `generate_sdk.py` —— 按 `SDK_COMMON.py` / `SDK_HMI.py` 的发布配置生成裁剪后的 SDK 包，参数见 `python scripts/generate_sdk.py --help`

---

## 1. 编译

用 `scripts/build.sh` 调 `west build`，参数对齐 `.vscode/tasks.json`。

### 常用命令

```bash
scripts/build.sh                        # 增量编译（默认 board = rtl87x3g_evb）
scripts/build.sh -p                     # 全量重编（pristine，等价 --pristine）
BOARD=rtl87x3g_evb scripts/build.sh     # 显式指定 board
DRY=1 scripts/build.sh                  # 只打印将要执行的命令，不真正编译
```

### 说明

- 默认 board：`rtl87x3g_evb`，可用环境变量 `BOARD=` 覆盖。
- 参数解析只认 `-p` / `--pristine`，其它未知参数会直接报错退出（防手滑）。
- 脚本自己 `cd` 到仓库根，无需担心当前目录。
- 编译产物：`build/`，最终烧录用的固件在 `bin/app.bin`（由 App 烧录脚本使用）。

### 常见问题

- **`west: command not found`**：先激活 Zephyr 环境（`source .venv/bin/activate` 或对应虚拟环境）。
- **改了 `prj.conf` / Kconfig 但没生效**：走 `scripts/build.sh -p` 触发一次 pristine。

---

## 2. 下载

### Linux → Windows 烧录（开发板接在 Windows）

适用于“Linux 编译、开发板连接 Windows 工位机”的场景。Linux 通过 SSH alias 登录 Windows，首次运行时把 `mpcli.exe`、烧录批处理和固件目录部署到 `%USERPROFILE%\.rtl87-flash`，之后上传 App 镜像并远程执行烧录。

```bash
scripts/flash-linux2win-app.sh COM4
```

默认 SSH 主机为 `main-win-pc`，COM 口为 `COM4`。主机、固件、地址和波特率可覆盖：

```bash
WIN_HOST=my-win-pc scripts/flash-linux2win-app.sh COM8
FW=/path/to/app.bin scripts/flash-linux2win-app.sh COM8
ADDR=0x7009E000 BAUD=2000000 scripts/flash-linux2win-app.sh COM8
DRY=1 scripts/flash-linux2win-app.sh COM4
```

Windows 端需要启用 OpenSSH Server，并配置 `ssh main-win-pc` 免交互登录。runtime 和 romfs 分别使用 `flash-linux2win-runtime.sh`、`flash-linux2win-romfs.sh`，三者共用同一份 Windows 缓存。

### 本地下载

用 `scripts/flash-linux-app.sh` 在原生 Linux 下直接调用 **Linux 版 `mpcli`**（原生 ELF，v4.0.0.7）。烧录参数对齐 `flash-wsl-app.sh`。

### 常用命令

```bash
scripts/flash-linux-app.sh                          # 默认 /dev/ttyUSB0
scripts/flash-linux-app.sh /dev/ttyUSB1             # 位置参数指定下载口
PORT=/dev/ttyUSB1 BAUD=3000000 scripts/flash-linux-app.sh  # 环境变量覆盖端口 / 波特率
DRY=1 scripts/flash-linux-app.sh                    # 只打印将要执行的命令，不真正烧录
```

### 默认配置

| 项 | 值 | 覆盖方式 |
|---|---|---|
| `mpcli` 路径 | `scripts/tool/mpcli/mpcli`（仓库自带 v4.0.0.7） | `MPCLI=` |
| 固件 | `<repo>/bin/app.bin`（**无 MP 头**） | `FW=` |
| 下载口 | `/dev/ttyUSB0` | 位置参数 或 `PORT=` |
| 波特率 | `2000000` | `BAUD=` |
| 烧录地址 | `0x7009E000` | `ADDR=` |
| 完整参数 | `-c <port> -T RTL87X3G -M 5 -p -A 0x7009E000 -F <fw> -b 2000000 -r -u -d` | — |

### 原理与注意事项

- 直接用 Linux 路径喂 `-F`，无需 `wslpath` 转换。
- 脚本会先检查 `mpcli`、`bin/app.bin`、串口是否存在且可写，缺任一就报错退出。
- 访问 `/dev/ttyUSB*` 需当前用户在 `dialout` 组：
  ```bash
  sudo usermod -aG dialout "$USER"   # 加组后需重新登录（或 newgrp dialout）才生效
  ```
- `mpcli`（frozen 打包）用 `sys.executable` 目录定位 `fw/` / `config/`，脚本 `cd` 到 mpcli 目录只是保持行为一致，不影响功能。

### 常见问题

- **`固件不存在: .../bin/app.bin`**：先跑一次 `scripts/build.sh`。
- **`mpcli 不存在或不可执行`**：改 `MPCLI=` 指到你实际的安装位置。
- **`串口不存在` / `串口不可写`**：检查 USB 连接（`ls /dev/ttyUSB*`），或把用户加入 `dialout` 组后重新登录。
- **端口占用 / 打不开**：多半是别的串口工具（如手动开的 `cat /dev/ttyUSB*`、`picocom`）还占着口，先关掉再 flash。

### 在 Windows / WSL 下烧录

WSL 和 Windows 原生脚本的参数集与 `flash-linux-app.sh` 完全一致（`-p -A 0x7009E000 -b 2000000 -M 5 -r -u -d -T RTL87X3G`），只是执行环境不同。两个脚本默认都从仓库自带的 `scripts/tool/mpcli/mpcli.exe`（v4.0.0.7）取工具，无需额外安装。

#### WSL2 里驱动 Windows 版 `mpcli.exe`

```bash
scripts/flash-wsl-app.sh COM8                    # 位置参数指定 COM 口（默认 COM13）
PORT=COM8 BAUD=2000000 scripts/flash-wsl-app.sh  # 环境变量覆盖
DRY=1 scripts/flash-wsl-app.sh COM8              # 只打印将要执行的命令，不真正烧录
```

- 默认 `mpcli.exe` 路径：`scripts/tool/mpcli/mpcli.exe`（仓库自带），用 `MPCLI_EXE=` 覆盖。
- 固件路径会用 `wslpath -w` 自动转成 Windows 形式喂给 `-F`。

#### Windows 原生 cmd（不进 WSL）

```bat
scripts\flash-win32-app.bat COM8              :: COM 口必须传，没有默认值
set BAUD=3000000 && scripts\flash-win32-app.bat COM8
set DRY=1 && scripts\flash-win32-app.bat COM8       :: 干跑，只打印命令
set NOPAUSE=1 && scripts\flash-win32-app.bat COM8   :: 结束不 pause（脚本 / tasks 调用用）
```

- 默认 `mpcli.exe` 路径：`scripts\tool\mpcli\mpcli.exe`（仓库自带），用 `set MPCLI_EXE=` 覆盖。
- 双击也可跑：先 `set PORT=COMx`，再双击 `flash-win32-app.bat`；执行完自动 `pause`，方便看输出。CI / tasks.json 里调用时把 `NOPAUSE=1` 打开即可。
- 可覆盖的环境变量：`PORT` / `BAUD` / `FW` / `ADDR` / `MPCLI_EXE` / `DRY` / `NOPAUSE`。

#### macOS 原生

```bash
scripts/flash-macos-app.sh                     # 只有一个 USB 串口时自动选它
scripts/flash-macos-app.sh /dev/cu.usbserial-1420

# 命名参数（推荐，不必记位置顺序）
scripts/flash-macos-app.sh --fw /path/app.bin --addr 0x7000A000 --port /dev/cu.usbserial-1420
scripts/flash-macos-app.sh --fw=/path/app.bin --addr=0x7000A000 --baud=3000000

# 位置参数按内容自动归类，顺序随意：/dev/... 是串口，0x... 是地址，其余当固件
scripts/flash-macos-app.sh /path/app.bin 0x7000A000 /dev/cu.usbserial-1420

# 环境变量写法同样支持（与 Linux/WSL 脚本一致）
PORT=/dev/cu.wchusbserial1420 BAUD=3000000 scripts/flash-macos-app.sh

DRY=1 scripts/flash-macos-app.sh               # 只打印将要执行的命令，不真正烧录
scripts/flash-macos-app.sh --help              # 打印脚本头部用法
```

优先级：**命令行参数 > 环境变量 > 默认值**（默认 `bin/app.bin` / `0x7009E000` / `2000000`）。

烧录参数与其它平台逐字一致（`-p -A 0x7009E000 -b 2000000 -M 5 -r -u -d -T RTL87X3G`），差别只在运行环境。三个脚本 `flash-macos-app.sh` / `flash-macos-runtime.sh` / `flash-macos-romfs.sh` 与 Linux、WSL、win32 版本一一对应。

三个 macOS 脚本都接受 `--fw` / `--addr` / `--port` / `--baud`（短名 `-f` / `-a` / `-c` / `-b`，也支持 `--opt=value`），runtime 另外接受 `--chip`。runtime 是固定 11 个镜像 + 固定地址表，**故意拒绝 `--fw` / `--addr`** 并提示改用 `flash-macos-app.sh`。

**mpcli-macos 放在哪：和其它平台的 mpcli 同一个目录。** 三个 macOS 脚本默认就用 `scripts/tool/mpcli/mpcli-macos`，与 `mpcli`（Linux ELF）、`mpcli.exe`（Windows）并排，**不需要任何指定**，和 `flash-linux-app.sh` 取 `tool/mpcli/mpcli` 的行为一致。只有要临时换二进制时才用 `MPCLI=` 覆盖：

```bash
MPCLI=$HOME/mpcli/mpcli scripts/flash-macos-app.sh
```

这个 Mach-O 二进制不在仓库里（构建产物），从源码自建——源码在 `D:\RemotePrj\mpcli\`（自带 `scripts/build_mac.sh`），**PyInstaller 不能交叉编译，必须在 macOS 上跑**：

```bash
# 在 macOS 上，于 mpcli 源码目录执行
bash scripts/build_mac.sh          # 产出 dist/mpcli-<ver>-macos-<arch>.tar.gz

# 解包后放到工具目录，改名以区别于 Linux ELF
tar -xzf dist/mpcli-*-macos-*.tar.gz -C /tmp
cp /tmp/mpcli-*-macos-*/mpcli <repo>/applications/eBadge_8773g_8711f/scripts/tool/mpcli/mpcli-macos
chmod +x <repo>/applications/eBadge_8773g_8711f/scripts/tool/mpcli/mpcli-macos
```

`build_mac.sh` 产出的是 host 架构单文件（Apple Silicon 上是 arm64，Intel 上是 x86_64），**没有 universal2**；要给两种 Mac 用就各自构建一份。

**`fw/` / `config/` 不用另外准备。** mpcli 的 `--flash-runtime` 通过 `mp_utility.get_root_path()` 找固件，打包版里该函数返回 `sys._MEIPASS` —— 也就是**二进制内部的解包临时目录**。`mpcli_setup_mac.spec` 已经把 `./fw` 和 `./config` 打进去了，所以 `mpcli-macos` 单独放进 `tool/mpcli/` 就能烧 runtime，目录里那份共用的 `fw/` 不是必需的（留着给 Linux/Windows 用）。

**log 目录必须可写。** `log.py` 把 log 写到**可执行文件同目录**的 `log/<date>/`，不可写就在启动时直接崩：

```
PermissionError: [Errno 13] Permission denied: '/usr/local/bin/log'
```

（已实测，仓库自带的 v4.0.0.7 同样如此。）`tool/mpcli/` 在仓库里，正常可写，所以放在默认位置不用管。只有你把 mpcli 挪到 `/usr/local/bin`、`/Applications` 这类只读位置时才需要处理 —— 三个脚本会自动兜底，把 `MPCLI_LOG_DIR` 指到 `scripts/log/`（该变量在源码 `log.py:105` 支持，v4.0.0.7 已含），也可以自己显式指定：

```bash
MPCLI_LOG_DIR=$HOME/.cache/mpcli scripts/flash-macos-app.sh
```

**串口设备名：用 `cu.*`，别用 `tty.*`。** macOS 上 `/dev/tty.usbserial-*` 的 `open()` 会等 DCD 载波，芯片没启动/没复位时会静默挂住（看起来像卡死）。脚本传进 `tty.*` 会自动换成对应的 `cu.*`；不传时按 `/dev/cu.usbserial*`、`/dev/cu.wchusbserial*`、`/dev/cu.SLAB_USBtoUART*`、`/dev/cu.usbmodem*` 探测——只有一个就用它，多个则报错让你显式指定（防烧错板子）。

> ⚠️ **`reboot_type=1` 会去开「前一个」串口，macOS 上会失败。** mpcli 的 `registerM5For87x3G.__get_reboot_port()` 按 `tty.usbserial` 前缀做字符串分割取 `-N` 减一，得到的是 **`tty.usbserial-xxxx`（没有 `/dev/` 前缀）**——那是个相对路径，肯定打不开。默认配置 `config/chip_configs/RTL87X3G/custom_config.ini` 里是 `is_reboot = False`，所以默认**不走这条路**；但一旦你为了「烧录前自动复位」把 `is_reboot` 改成 `True`，macOS 上就会报 `fail tot get debug board port` / `open ... fail`。要在 macOS 上自动复位，用 `reboot_type = 0`（HCI reboot，走 `-c` 指定的同一个口）或 `2`（CLI bud reboot），别用 `1`。

**macOS 常见问题**

- **`Bad CPU type in executable`**：Apple Silicon 上跑 x86_64 版 mpcli，缺 Rosetta 2：
  ```bash
  softwareupdate --install-rosetta --agree-to-license
  ```
  脚本会先用 `lipo -archs` 检查并提前警告。
- **`cannot be opened because the developer cannot be verified`** / Gatekeeper 拦截：给二进制去掉隔离属性
  ```bash
  xattr -d com.apple.quarantine scripts/tool/mpcli/mpcli-macos
  ```
  若报 `killed`：可能是签名问题，`codesign --force --sign - scripts/tool/mpcli/mpcli-macos` 做一次本地自签名。
- **`Resource temporarily unavailable` / 端口忙**：别的串口工具（CoolTerm、minicom、`screen`、Arduino IDE 串口监视器）还占着口，先退出。`screen /dev/cu.usbserial-1420 2000000` 用完要 `Ctrl+A K` 正常退出，直接关终端窗口会留下占用。
- **`没有那个文件或目录` 但设备在**：`ls /dev/cu.*` 确认名字，CH340/CP2102/FTDI 的命名不同（`cu.wchusbserial*` / `cu.SLAB_USBtoUART*` / `cu.usbserial-*`）。找不到先装对应驱动（CH34x 在 macOS 13+ 需要厂商最新驱动）。
- **`PermissionError: ... log`**：mpcli 放在只读目录了，见上面「log 目录必须可写」。
- **`固件不存在: .../bin/app.bin`**：先跑一次 `scripts/build.sh`（macOS 上编译需要相应工具链，本仓库默认流程仍以 Linux 为准）。

---

## 2.5 烧录 runtime

各 App 烧录脚本只烧**单个 App 镜像**（`-p -A 0x7009E000 -F bin/app.bin`）。换板子、刷新 BT stack / DSP、或板子是空片时，还得先烧一套 **runtime**。这由 `flash-runtime-*` 和 `flash-linux2win-runtime.sh` 负责，内部调 mpcli 的 `--flash-runtime`，生成 `flash_image.json` 后全刷。

### 常用命令

```bash
scripts/flash-runtime-linux.sh                        # 原生 Linux，默认 /dev/ttyUSB0
scripts/flash-runtime-linux.sh /dev/ttyUSB1           # 位置参数指定下载口
CHIP=RTL8783G scripts/flash-runtime-linux.sh          # 换芯片
DRY=1 scripts/flash-runtime-linux.sh                  # 干跑，不接板子也能看命令

scripts/flash-runtime-wsl.sh COM8                     # WSL 驱动 mpcli.exe（默认 COM13）
scripts/flash-linux2win-runtime.sh COM8               # Linux 通过 SSH 驱动 Windows 烧录（默认 COM4）
CHIP=RTL8783G scripts/flash-linux2win-runtime.sh COM8
DRY=1 scripts/flash-linux2win-runtime.sh COM8         # 只打印配置，不连接 Windows
```

```bat
scripts\flash-runtime-win32.bat COM8                  :: Windows 原生 cmd，COM 口必须传
set CHIP=RTL8783G && scripts\flash-runtime-win32.bat COM8
```

```bash
scripts/flash-macos-runtime.sh                        # macOS，自动探测 /dev/cu.* 下载口
scripts/flash-macos-runtime.sh --chip RTL8783G        # 换芯片（也可位置参数：<dev> <chip>）
CHIP=RTL8783G scripts/flash-macos-runtime.sh          # 环境变量写法
DRY=1 scripts/flash-macos-runtime.sh                  # 干跑，不接板子也能看命令
```

### 默认配置

| 项 | 值 | 覆盖方式 |
|---|---|---|
| 芯片 | `RTL8773G`（取 `tool/mpcli/fw/runtime_system_bin/` 下的目录名，可选 `RTL8773G` / `RTL8783G` / `RTL87X3EP`） | `CHIP=` |
| 下载口 | `/dev/ttyUSB0`（linux）、`COM13`（wsl）、`COM4`（linux2win）、无默认（win32）、自动探测 `/dev/cu.*`（macOS） | 位置参数 或 `PORT=` |
| 波特率 | `2000000` | `BAUD=` |
| 完整参数 | `--flash-runtime <CHIP> -c <port> -u -d -b 2000000` | — |

`-T` 不用传，mpcli 从 `CHIP` 自动推导（`RTL8773G` → `RTL87X3G`）。

`flash-linux2win-runtime.sh` 用于“Linux 编译、开发板接在 Windows”的场景。Linux 通过 SSH
连接 `WIN_HOST`（默认 `main-win-pc`），首次运行时把 `mpcli.exe`、App/runtime 批处理和
runtime 固件目录部署到 Windows 的 `%USERPROFILE%\.rtl87-flash`，之后直接远程执行
`flash-runtime-win32.bat`：

```bash
scripts/flash-linux2win-runtime.sh COM4
WIN_HOST=my-win-pc CHIP=RTL8783G scripts/flash-linux2win-runtime.sh COM8
```

该脚本与 `flash-linux2win-app.sh`、`flash-linux2win-romfs.sh` 共用 Windows 缓存。runtime
烧完后仍需执行 `scripts/flash-linux2win-app.sh COM4` 烧入 App。

### 注意事项

- **runtime 不含 APP**。烧的 11 个镜像地址是 `0x70002000`(SYSTEM_Config) / `4000`+`7000`(boot patch0/1) / `a000`(upperstack) / `49000`(OTA header) / `4a000`(stack patch) / `7c000`(sys patch) / `21e000`+`2bc000`+`2fc000`(DSP sys/app/cfg) / `306000`(APP_Config)，**没有 APP 的 `0x7009E000`**。完整刷机要两步：
  ```bash
  scripts/flash-runtime-linux.sh          # 1. 先烧 runtime
  scripts/flash-linux-app.sh              # 2. 再烧 bin/app.bin
  ```
- **`-u -d` 是脚本显式加的，别去掉**。mpcli 的 `--flash-runtime` 只注入 `-M 5 -r -b 3000000`，而 `SYSTEM_Config` 落在受保护的 `0x70002000`（OEM_CFG 区），少了 `-u` 写不进去。
- `--flash-runtime` **不支持** `--gen-output-dir`，生成的 `flash_image.json` / `flash_image_report.txt` 必然落在 `fw/runtime_system_bin/<CHIP>/`。已在 `tool/mpcli/.gitignore` 里忽略，不会污染仓库。
- WSL 版走 UNC 路径（`\\wsl.localhost\...`）读固件，比原生 Linux 慢（扫 11 个 bin 约 2.5s vs 0.03s）。嫌慢就把 `fw/runtime_system_bin/<CHIP>` 拷到 Windows 盘，改用两步法：
  ```bash
  cd scripts/tool/mpcli
  ./mpcli.exe --gen-json --bin-dir <Windows 盘上的固件目录> --gen-output-dir <输出目录>
  ./mpcli.exe -f <输出目录>\flash_image.json -a -c COM8 -T RTL87X3G -b 2000000 -M 5 -r -u -d
  ```

### 烧其他固件目录 / 单个镜像

- **烧 SDK 构建输出或第三方固件包**（目录里要有带 MP header 的 `.bin` 和 `flash_map.ini`）：
  ```bash
  cd scripts/tool/mpcli
  ./mpcli --gen-json --bin-dir <固件目录> -T RTL87X3G -c /dev/ttyUSB0 -u -d -b 2000000
  ```
- **只烧某一个镜像**：现有 `flash-*` 脚本的 `ADDR=` / `FW=` 就够用，地址查上面列表或 `fw/runtime_system_bin/<CHIP>/flash_map.ini`：
  ```bash
  ADDR=0x7000A000 FW=/path/to/upperstack.bin scripts/flash-linux-app.sh
  ```

---

## 2.6 烧录 romfs

HoneyGUI 资源镜像是独立于 App 的裸镜像，烧录地址必须与 `ui/project.json` 的 `romfsBaseAddr`、`gui_vfs_mount_romfs()` 调用以及分区表地址一致。地址必须按 4 KB 对齐。

### WSL2

```bash
scripts/flash-wsl-romfs.sh ../ui/build/app_romfs_0x704D1000.bin 0x704D1000 COM8
```

### Linux → Windows

```bash
scripts/flash-linux2win-romfs.sh ../ui/build/app_romfs_0x704D1000.bin 0x704D1000 COM4
WIN_HOST=my-win-pc DRY=1 scripts/flash-linux2win-romfs.sh
```

### macOS

```bash
scripts/flash-macos-romfs.sh ../ui/build/app_romfs_0x70536000.bin              # 地址从文件名推导
scripts/flash-macos-romfs.sh ../ui/build/app_romfs_0x70536000.bin 0x70536000 /dev/cu.usbserial-1420
scripts/flash-macos-romfs.sh --fw ../ui/build/app_romfs_0x70536000.bin --port /dev/cu.usbserial-1420
DRY=1 scripts/flash-macos-romfs.sh ../ui/build/app_romfs_0x70536000.bin
```

> 分区地址以本 App 的 `flash_map.h` 为准：`USER_DATA1_ADDR = 0x70536000`（其它 App 目录下的分区表不同，上面的 `0x704D1000` 是别的工程的旧值——**烧之前先 `grep USER_DATA1_ADDR flash_map.h` 确认**）。

脚本会检查地址对齐、镜像内烘焙的基址和 romfs root dirent。资源镜像与 App、runtime 使用不同的 flash 偏移。

---

## 3. 查看 LOG

原生 Linux 下**不用** `serial_term.py`（它依赖 Windows 的 `msvcrt`，跑不了）。用系统现成命令即可：`stty` 配一次串口参数，`cat` 实时看、顺手 `tee` 存盘。

> **macOS 上看 log**：没有 `stty -F`（BSD 是 `stty -f`），串口是 `/dev/cu.*`：
> ```bash
> PORT=/dev/cu.usbserial-1420; BAUD=2000000
> stty -f "$PORT" "$BAUD" cs8 -cstopb -parenb -crtscts clocal -hupcl raw -echo
> stdbuf -oL cat "$PORT" | tee scripts/log/$(date +%Y%m%d_%H%M%S).log   # stdbuf 需 brew install coreutils
> ```
> 没有 `stdbuf` 就用 `cat "$PORT" | tee -a file`（macOS 的 `cat` 写成管道时也是全缓冲，boot log 会卡住不落盘），或 `script -q /dev/null cat "$PORT"`、`picocom -b 2000000 /dev/cu.usbserial-1420`、`tio -b 2000000 /dev/cu.usbserial-1420`。

> ⚠️ **RTS/DTR 与复位**：RTL87X3G 的 RTS 是**低有效复位线**。很多串口工具打开时会拉低 RTS/DTR 把芯片摁复位。下面用 `stty` 关掉硬件流控（`-crtscts`）并关 `hupcl`，避免打开/关闭串口时误复位。若你的 USB 转串口驱动在 open 时仍会拨动 DTR/RTS，看到芯片被复位是正常现象，重新连接即可。

### 一次性配置串口参数

```bash
PORT=/dev/ttyUSB1        # 看 log 的口（下载口通常是 ttyUSB0，log 口另一个）
BAUD=2000000

# 8N1、无流控、raw、不因打开/关闭而挂断复位
stty -F "$PORT" "$BAUD" cs8 -cstopb -parenb -crtscts -hupcl raw -echo
```

### 实时看 log（可同时存盘）

```bash
# 只看（直接输出到终端）——终端是行缓冲，能实时刷出，ANSI 颜色也会被渲染
cat "$PORT"
```

⚠️ **别用 `cat "$PORT" | tee file` 或 `cat "$PORT" > file` 存盘**——一旦 `cat` 的
stdout 不是终端（是管道或文件），glibc 会把它切成 **4KB 全缓冲**，攒不满 4KB
就不落盘。boot log 往往只有几百字节到 1KB，于是你会看到"文件一直是 0 字节 /
`tail -f` 什么都没有"，其实数据卡在 `cat` 进程的内存缓冲里没写出来。这个坑实测踩过。

**存盘要强制行缓冲或无缓冲**，用下面任一种：

```bash
mkdir -p scripts/log
LOG="scripts/log/$(date +%Y%m%d_%H%M%S)_${PORT//\//_}.log"

# 方式 A（推荐）：stdbuf 强制行缓冲，边看边存
stdbuf -oL cat "$PORT" | tee "$LOG"

# 方式 B：dd 每字节直写文件，绝对无缓冲（只存不看；另开终端 tail -f 看）
dd if="$PORT" of="$LOG" bs=1
```

`Ctrl+C` 停止。文件名里的 `${PORT//\//_}` 会把 `/dev/ttyUSB1` 变成 `_dev_ttyUSB1`。

> **抓开机 boot log 的正确姿势**：boot 信息只在上电/复位那一瞬间打印，错过就没了。
> 所以要 **先起监测、再复位**：
> 1. 先在一个终端跑 `stdbuf -oL cat "$PORT" | tee "$LOG"`（或 `dd ...`）；
> 2. 监测起来后再按板上复位键 / 重新烧录（App 烧录脚本结尾会 `Reboot`）；
> 3. 正在监听的进程就能完整抓到 `Booting Zephyr OS ...` 那一段。
> 顺序反了（先复位再起 cat）就只能抓到 boot 之后的静默，看着像"没输出"。

> 若装了 `picocom` / `tio` 也可以用（它们自带存盘、且能收发）：
> `picocom -b 2000000 --imap lfcrlf /dev/ttyUSB1` 或 `tio -b 2000000 /dev/ttyUSB1`。
> 注意 `picocom` 默认会拨 DTR/RTS，加 `--lower-rts --lower-dtr` 之类选项按需规避复位。

### 看 log ↔ 下载共用一个端口

`cat` / `picocom` 会占着串口，下载前先停掉：

1. 终端里 `cat "$PORT"` 看 log
2. `Ctrl+C` 停掉，释放端口
3. `scripts/flash-linux-app.sh` 下载固件
4. 再 `cat "$PORT"` 继续看

### 怎么看已存下来的 log

Log 里是**芯片吐出来的原始字节**，含 ANSI 转义（颜色、光标控制）。

#### 推荐：能识别 ANSI 的工具

- **`less -R`**（翻长 log 首选）：
  ```bash
  less -R scripts/log/20260717_160000__dev_ttyUSB1.log
  ```
  `-R` 让 ANSI 序列按颜色渲染，而不是显示成 `^[[31m`。
- **`cat`**：直接 `cat` 文件，终端会解释 ANSI。
- **`tail -f`**：跟随最新的一份：
  ```bash
  tail -f "$(ls -t scripts/log/*.log | head -1)"
  ```

#### 想去掉 ANSI 只留纯文本

```bash
# 剥 ANSI 转义
sed -r 's/\x1b\[[0-9;]*[a-zA-Z]//g' scripts/log/20260717_160000__dev_ttyUSB1.log > clean.log

# 或者用 ansi2txt（需安装 colorized-logs 包：sudo apt install colorized-logs）
ansi2txt < scripts/log/20260717_160000__dev_ttyUSB1.log > clean.log
```

#### 想搜特定关键字

```bash
LOGDIR=scripts/log

grep -n "assert"        "$LOGDIR"/*.log       # 直接 grep，ANSI 不影响文本匹配
grep -n -C3 "hardfault" "$LOGDIR"/*.log       # 带上下文
grep -n -C3 "hardfault" "$(ls -t "$LOGDIR"/*.log | head -1)"  # 只在最新那份里找
```

### 备注

- **端口权限**：读 `/dev/ttyUSB*` 同样需要 `dialout` 组（见「下载」章节）。
- **"看不到输出"排错顺序**：
  1. **存盘却是 0 字节 / `tail -f` 空** → 十有八九是 `cat | tee` / `cat > file` 的
     全缓冲坑，改用 `stdbuf -oL cat` 或 `dd bs=1`（见上）。先发个命令验证通路：
     `printf 'kernel version\r\n' > "$PORT"`，正常会回 `Zephyr version 3.7.2`。
  2. **直接 `cat "$PORT"` 到终端也没东西** → 设备此刻不主动打印，按板上复位键，
     或触发一次会打印的操作（boot log 只在复位瞬间打）。
  3. **收到乱码** → `stty` 的波特率和固件不一致，确认都是 `2000000`。
- **log 越攒越大**：`scripts/log/` 不会自动清理，按需 `rm scripts/log/2026*.log`。
