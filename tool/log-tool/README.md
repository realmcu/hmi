# Log Tools 使用说明

```text
tool/log-tool/
  DebugAnalyzer/          Windows：DebugAnalyzer.exe
  ConsoleDebugAnalyzer/   Linux x86_64：ConsoleDebugAnalyzer.out
```

两个版本是独立程序，必须使用同一份固件构建出的 trace。

## Windows

先关闭 PuTTY、串口助手等占用 `COM3` 的程序。当前 Windows 工作区映射为
`X:\workspace\watch-360360`，在 PowerShell 中运行：

```powershell
$root  = "X:\workspace\watch-360360"
$trace = "$root\zephyrproject\realtek-app\bin\rtl8773g\trace"
$out   = "$root\scripts\tool\log-tool\ConsoleDebugAnalyzer\output"

Set-Location "$root\scripts\tool\log-tool\DebugAnalyzer"
.\DebugAnalyzer.exe -j0 -fs -sp COM3 -br 2000000 `
  -ar  "$trace\ROM.trace" `
  -ri  "$trace\LOG1.h" `
  -apf "$root\applications\bin\app.trace" `
  -aus "$trace\upperstack.trace" `
  -aa  "$trace\app_honeygui.trace" `
  -ap  "$trace\boot_patch0.trace" `
  -pi  "$trace\patch_log.h" `
  -spi "$trace\stack_patch_log.h" `
  -da  "$trace\DSP_APP.trace" `
  -du  "$trace\DSP_SDK_AUDIO.trace" `
  -dv  "$trace\DSP_SDK_VOICE.trace" `
  -fp  "$out"
```

`-j0` 表示不显示 UI，解码结果持续输出并写入 `$out`，按 `Ctrl+C` 停止。抓 boot log 时，
先执行上面的命令，再复位板子：

```powershell
C:\Users\howie_wang\.rtl87-flash\tool\mpcli\mpcli.exe `
  -c COM4 -T RTL87X3G -M 5 -b 2000000 -r
```

成功日志应包含 `[BOOT]`、`ROM MP Loader`、`boot patch` 和 `osif-zephyr init success!`。当前 watch-360360 已实测解码 45 行 `[BOOT]`，0 条解码错误。

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
