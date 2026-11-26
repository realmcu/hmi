# watch-360360 applications

RTL87X3G 智能手表应用（Zephyr / west + HoneyGUI，360x360）。本目录是应用工程本体，
源码入口是 `main.c`，业务模块在 `app/`；构建与烧录脚本在 workspace 根目录的 `scripts/`。

除特别说明外，命令都在 workspace 根目录执行。

## 0. 闭环调试一览

改了什么代码，就决定要重新生成什么、烧哪一份镜像、盯哪一路 log：

| 改动 | 重新生成 | 下载 | 观察 |
|---|---|---|---|
| 应用 C 代码 / `prj.conf` / board overlay | `scripts/build.sh` | `scripts/flash-linux2win-app.sh` | 手表 `COM3` |
| UI：HML、图片、字体 | HoneyGUI Designer codegen + export，产出 `ui/build/app_romfs_0x*.bin` | `scripts/flash-linux2win-romfs.sh <bin> 0x704D1000 COM4` | 手表 `COM3` |
| runtime / SDK / 分区 / 换板 | 无需编译 | runtime -> romfs -> app（见 2.2） | 手表 `COM3` |
| 手机 App | `flutter run`（热重载）或 `flutter build apk` | `adb install -r` | `adb logcat` |

典型一次迭代：

1. 先开 log：`ssh -t main-win-pc`，按 4.1 启动 `log-tool` 的 `DebugAnalyzer.exe`。
2. 改代码，`scripts/build.sh`。
3. `scripts/flash-linux2win-app.sh`，脚本结束会 Reboot。
4. 看 `COM3` 上的启动 log；对应模块开启日志时再检查业务输出。手机侧另开一个终端
   `adb logcat -s flutter`。
5. 没抓到现象就复位板子重来，改完回到第 2 步。

每一步的通过判据：

- 编译：`applications/bin/app.bin` 时间戳更新，没有 error。
- 下载：出现 `CMD: program ... ok` / `CMD: flash verify ... ok`，
  最后 `Download : Success` 和 `Reboot OK`。
- 运行：`COM3` 上出现新的 `[BOOT]`、`ROM MP Loader`、boot patch 和
  `osif-zephyr init success!`，而不是只有复位前的旧内容。
- 手机：`adb logcat -s flutter` 里能看到扫描、连接、订阅的打印，App 不崩。

只改 UI 时不必重烧 App：romfs 是独立镜像，按 `USER_DATA1_ADDR` 单独烧即可，前提是
`romfsBaseAddr`、`gui_vfs_mount_romfs()` 和分区表三处的地址没变。

## 1. 编译

```bash
scripts/build.sh          # 增量编译，默认 board = rtl87x3g_evb
scripts/build.sh -p       # 全量重新编译（pristine）
```

- 产物在 `applications/build/`，烧录用固件是 `applications/bin/app.bin`（无 MP header）。
- 改了 `prj.conf` / `Kconfig` 但不生效时，用 `-p` 全量编译。
- 报 `west: command not found` 时先激活 Zephyr 虚拟环境。

## 2. 下载

板子接在 Windows 工作机上，Linux 侧通过 SSH（默认 `main-win-pc`，串口默认 `COM4`）
驱动 Windows 上的 `mpcli.exe` 烧录。首次运行会把工具环境部署到
`%USERPROFILE%\.rtl87-flash`，之后复用缓存。

### 2.1 日常下载：只烧 App

改完应用代码、重新编译之后，正常情况下只需要烧 App：

```bash
scripts/flash-linux2win-app.sh              # 默认 COM4
scripts/flash-linux2win-app.sh COM8         # 指定串口
DRY=1 scripts/flash-linux2win-app.sh        # 只打印参数，不连接板子
```

会把 `applications/bin/app.bin` 烧到 `0x7009E000`，完成后自动 Reboot。

### 2.2 特殊场景：完整重新烧录

换板子、换 runtime / SDK、分区或 romfs 地址有变动时，才需要完整烧录。完整流程是
**runtime -> romfs -> App** 三步，顺序不能颠倒，runtime 本身不包含 App：

```bash
scripts/flash-linux2win-runtime.sh  COM4
scripts/flash-linux2win-romfs.sh    ui/build/app_romfs_0x704D1000.bin 0x704D1000 COM4
scripts/flash-linux2win-app.sh      COM4
```

- runtime 是 11 个镜像的固定集合（boot patch / upperstack / stack patch / sys patch /
  DSP / Config 等），地址表由脚本内置，不接受自定义固件地址。
- romfs 是 HoneyGUI 资源镜像，地址必须与 `flash_map.h` 中的
  `USER_DATA1_ADDR`（当前 `0x704D1000`）和 `ui/project.json` 的
  `romfsBaseAddr` 一致，并且 4 KB 对齐。地址不匹配烧录不会报错，但启动时会
  `open file fail` 或 assertion。
- romfs 镜像由 UI 工程构建产生，路径形如 `ui/build/app_romfs_<addr>.bin`。
- 三个脚本共用同一个 Windows 缓存，连续执行不会重复部署工具。

### 2.3 可覆盖的参数

| 变量 | 默认值 | 说明 |
|---|---|---|
| `WIN_HOST` | `main-win-pc` | 板子所连 Windows 的 SSH 别名 |
| 第一个位置参数 / `PORT` | `COM4` | 下载串口 |
| `FW` | `applications/bin/app.bin` | App 固件 |
| `ADDR` | `0x7009E000` | App 烧录地址 |
| `BAUD` | `2000000` | 波特率 |
| `CHIP` | `RTL8773G` | runtime 芯片型号（另支持 `RTL8783G` / `RTL87X3EP`） |
| `DRY` | `0` | 设为 `1` 只打印配置 |

```bash
WIN_HOST=my-win-pc scripts/flash-linux2win-app.sh COM8
FW=/path/to/app.bin ADDR=0x7009E000 scripts/flash-linux2win-app.sh COM8
CHIP=RTL8783G scripts/flash-linux2win-runtime.sh COM8
```

### 2.4 备用方式：通过 HoneyBox 内置固件 OTA

如果 `COM4`/`mpcli` 无法建立下载连接，但手表仍能正常启动并支持 BLE OTA，可以临时把
App 固件打包进 HoneyBox APK，通过手机给手表做 OTA 升级，绕开串口下载。

1. 生成 RTL8773G 可接受的 MP OTA 固件包，并替换 HoneyBox 中的内置文件：

   ```text
   /home/wh/workspace/HoneyBox/assets/ota/watch_rtl8773g_app_mp.bin
   ```

   Watch OTA 页面固定读取这个文件；不能直接把裸 `applications/bin/app.bin` 当作 MP 包
   放进去。

2. 重新构建并安装 HoneyBox APK，使新的固件包进入 APK 资源：

   ```bash
   cd /home/wh/workspace/HoneyBox
   flutter build apk --debug
   adb install -r build/app/outputs/flutter-apk/app-debug.apk
   ```

3. 手机连接 Watch，进入“固件 OTA 升级”，点击“开始升级”并确认风险。升级完成后，
   App 会检查固件、等待手表重启并自动重连。

这条通道只用于更新 App 镜像，不能替代 runtime、romfs 或完整烧录；如果当前固件无法
启动、没有 BLE OTA 能力，或者问题位于 boot/runtime/分区层，仍然必须修复
`COM4`/`mpcli` 后按 2.1 或 2.2 重新烧录。

## 3. 手机联调（HoneyBox APK）

手表和手机一起联调时，手机侧用 `HoneyBox` 这个 Flutter 调试应用，它是独立仓库：

```bash
cd /home/wh/workspace/HoneyBox             # git@github.com:realmcu/HoneyBox.git
```

- 支持 Android 和 Windows，Android 通过 BLE 与手表通信；设备列表里匹配到 JW GATT
  服务和特征后进入 Watch 页面。
- Watch 协议、能力限制和命令行验收方式见该仓库的 `docs/jw-sdk-guide.md`。
- 本机 Flutter 在 `/home/wh/.local/Android/flutter/bin`，adb 在
  `/home/wh/Android/Sdk/platform-tools`。非交互 shell 不会加载 `~/.bashrc`，
  需要自己把这两个目录加进 `PATH`。

构建并安装 APK（手机已开 USB 调试）：

```bash
cd /home/wh/workspace/HoneyBox
export PATH="$HOME/.local/Android/flutter/bin:$HOME/Android/Sdk/platform-tools:$PATH"
flutter pub get
flutter build apk --debug
adb devices
adb install -r build/app/outputs/flutter-apk/app-debug.apk
```

也可以直接 `flutter run -d <android-device-id>` 一步编译、安装并拉起日志；运行中
`r` 热重载、`R` 热重启、`q` 退出，改 Dart 代码不用重装 APK。
Release 包是 `build/app/outputs/apk/release/HoneyBox-release.apk`，
构建前需要 `android/local.properties` 里的正式签名和高德 Key，缺项会直接失败
（详见 HoneyBox 的 `README.md`）。

## 4. 查看 LOG

### 4.1 手表 LOG（COM3，2 Mbaud）

板子的 log 口在 Windows 工作机上，串口是 `COM3`，波特率 `2000000`。登录 Windows
后运行 `capture-log.ps1`：它会先把 `DebugAnalyzer` 和当前固件的 trace 复制到 Windows
本地目录，再从本地启动，避免映射盘 EXE 被执行策略拒绝。

```bash
ssh -t main-win-pc
```

远程 shell 是 `cmd.exe`，直接运行：

```powershell
powershell -ExecutionPolicy Bypass -File X:\workspace\watch-360360\scripts\tool\log-tool\capture-log.ps1
```

- `ssh -t` 必须有，否则远程命令没有交互式终端，无法用 `Ctrl+C` 停止抓取。
- 启动前先关闭 PuTTY、串口助手等占用 `COM3` 的程序。
- 默认读取 `COM3 @ 2000000`；需要其他串口或波特率时使用 `-Port COMx` / `-Baud n`。
  例如：`capture-log.ps1 -Port COM5`。
- 停止时按 `Ctrl+C`。脚本会终止 `DebugAnalyzer`，并把 `.log` / `.bin` 回拷到
  `scripts/tool/log-tool/ConsoleDebugAnalyzer/output/`；`-KeepRemote` 可保留 Windows 本地副本。
- trace 必须和板子上的固件来自同一次构建，尤其是重新执行 `scripts/build.sh` 后的
  `applications/bin/app.trace`。trace 不匹配时会出现模块错误、漏行或乱码。
- 下载用 `COM4`，看 log 用 `COM3`，两个口通常互不抢占；如果实际是同一根线，先停止
  `DebugAnalyzer.exe` 释放串口，烧完再重新启动。
- **抓启动 log 必须触发 reset**。只抓 log 时，先启动上面的抓取命令，再另开一个 Windows
  终端执行：

  ```bat
  %USERPROFILE%\.rtl87-flash\tool\mpcli\mpcli.exe -c COM4 -T RTL87X3G -M 5 -b 2000000 -r
  ```

  成功时会看到 `Reboot OK!`，抓取文件随后出现新的 `[BOOT]`。如果下一步直接执行
  `scripts/flash-linux2win-app.sh`，烧录结束的 Reboot 可以替代这条单独 reset 命令。
  完整工具说明见
  `scripts/tool/log-tool/README.md`。

### 4.2 手机 LOG（adb）

手机用 USB 连开发机，直接看 adb 日志：

```bash
export PATH="$HOME/Android/Sdk/platform-tools:$PATH"
adb devices
adb logcat -v time            # 全量
adb logcat -s flutter         # 只看 Flutter / Dart 的 print、debugPrint
adb logcat -c                 # 复现前先清掉旧日志
```

联调 BLE 时通常两个终端并排：一个运行 `DebugAnalyzer.exe` 看手表 `COM3`，一个
`adb logcat -s flutter`。

## 5. 更多

本地 Linux / WSL / 原生 Windows / macOS 的烧录方式、串口日志抓取、runtime 与 romfs
的细节，见 `scripts/README.md`。
