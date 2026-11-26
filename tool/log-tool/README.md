# Log Tools 使用说明

```text
tool/log-tool/
  DebugAnalyzer/          Windows：DebugAnalyzer.exe
  ConsoleDebugAnalyzer/   Linux x86_64：ConsoleDebugAnalyzer.out
```

两个版本是独立程序，必须使用同一份固件构建出的 trace。

## Windows

先关闭 PuTTY、串口助手等占用 `COM3` 的程序。当前 Windows 工作区映射为
`X:\workspace\watch-360360`。映射盘上的 EXE 可能被执行策略拒绝，因此使用
`capture-log.ps1` 先把工具和 trace 同步到 Windows 本地目录再运行：

```powershell
powershell -ExecutionPolicy Bypass -File X:\workspace\watch-360360\scripts\tool\log-tool\capture-log.ps1
```

脚本默认抓取 `COM3 @ 2000000`，可用 `-Port COMx` / `-Baud n` 覆盖。例如指定 `COM5`：

```powershell
powershell -ExecutionPolicy Bypass -File X:\workspace\watch-360360\scripts\tool\log-tool\capture-log.ps1 -Port COM5
```

工具和 trace 会复制到
`%USERPROFILE%\.rtl87-flash\log-tool` 后运行，避免直接执行映射盘上的 EXE。

开始抓取后还需要 reset 一次，板子才会重新输出 boot log。另开一个 Windows 终端执行：

```bat
%USERPROFILE%\.rtl87-flash\tool\mpcli\mpcli.exe -c COM4 -T RTL87X3G -M 5 -b 2000000 -r
```

成功时显示 `Reboot OK!`。如果接下来直接烧录，烧录脚本末尾的 Reboot 也可以替代单独的
reset。

成功日志应包含 `[BOOT]`、`ROM MP Loader`、`boot patch` 和 `osif-zephyr init success!`。当前 watch-360360 实测：单次复位启动为 45 行 `[BOOT]`，完整烧录过程为 659 行，
0 条解码错误；跨复位点可能出现 `DebugTool info: Maybe there are missing logs!`，这是
抓取切换提示，不等于解码错误。

按 `Ctrl+C` 停止后，`COM3_<时间>.log` / `.bin` 保留在工具实际运行的 Windows 目录：
`%USERPROFILE%\.rtl87-flash\log-tool\output\`，不会回拷到 Linux workspace。

## Linux

```bash
cd /home/wh/workspace/watch-360360/scripts/tool/log-tool/ConsoleDebugAnalyzer
chmod +x ConsoleDebugAnalyzer.out
./ConsoleDebugAnalyzer.out --help
ls /dev/ttyUSB* /dev/ttyACM*
./ConsoleDebugAnalyzer.out -p /dev/ttyUSB0 -b 2000000 -partNum RTL87X3G -f ./config.json
```

按 `Ctrl+C` 停止。`config.json` 已配置当前 watch-360360 工程的 trace 和输出目录；换固件后要同步更新 trace。Linux 版不能使用 Windows 的 `COM3`。

## 当前工程 trace

| 用途 | 文件 |
|---|---|
| ROM（`-ar` / `romTraceFile`） | `zephyrproject/realtek-app/bin/rtl8773g/trace/ROM.trace` |
| Platform（`-apf` / `platformTraceFile`） | `applications/bin/app.trace` |
| Upper stack（`-aus` / `upperstackTraceFile`） | `zephyrproject/realtek-app/bin/rtl8773g/trace/upperstack.trace` |
| App（`-aa` / `appTraceFile`） | `zephyrproject/realtek-app/bin/rtl8773g/trace/app_honeygui.trace` |
| Patch（`-ap` / `patchTraceFile`） | `zephyrproject/realtek-app/bin/rtl8773g/trace/boot_patch0.trace` |
| DSP（`-da` / `-du` / `-dv`） | `zephyrproject/realtek-app/bin/rtl8773g/trace/DSP_*.trace` |

## 常见问题

- `对端口“COM3”的访问被拒绝`：串口被其他工具占用，先关闭占用程序。
- 只有版本头和很小的 `.bin`：通常是板子已经启动完才开始监听；先启动 `DebugAnalyzer.exe` 或 `ConsoleDebugAnalyzer.out`，再复位板子。
- 日志乱码、缺少参数或模块错误：trace 与固件版本不匹配，重新使用同一次构建的 trace。
