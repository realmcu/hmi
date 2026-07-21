# scripts

本目录下的常用工具脚本，覆盖 RTL87X3G 应用的**编译 → 下载 → 抓 log** 完整链路。所有脚本默认从**仓库根目录**跑也行、从 `scripts/` 里跑也行，路径都是相对 `scripts/` 自身解析的。

- `build.sh` —— `west build` 封装
- `flash.sh` —— WSL 里驱动 Windows 版 `mpcli.exe` 烧录
- `serial_term.py` —— **仅备份**。实际抓 log 的脚本跑在 Windows 侧（`C:\Users\howie_wang.RSDOMAIN\serial_term.py`），本目录这份**不要执行**，详见「查看 LOG」章节

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
- 编译产物：`build/`，最终烧录用的固件在 `bin/app.bin`（由 `flash.sh` 使用）。

### 常见问题

- **`west: command not found`**：先激活 Zephyr 环境（`source .venv/bin/activate` 或对应虚拟环境）。
- **改了 `prj.conf` / Kconfig 但没生效**：走 `scripts/build.sh -p` 触发一次 pristine。

---

## 2. 下载

用 `scripts/flash.sh` 在 WSL2 里直接调用 **Windows 版 `mpcli.exe`**（原生进程，才能访问 COM 口）。参数对齐 `.vscode/tasks.json` 的 `West Flash`。

### 常用命令

```bash
scripts/flash.sh                        # 默认 COM13
scripts/flash.sh COM8                   # 位置参数指定下载口
PORT=COM8 BAUD=2000000 scripts/flash.sh # 环境变量覆盖端口 / 波特率
DRY=1 scripts/flash.sh                  # 只打印将要执行的命令，不真正烧录
```

### 默认配置

| 项 | 值 | 覆盖方式 |
|---|---|---|
| `mpcli.exe` 路径 | `/mnt/d/mpcli_meta_tool_v4.0.0.6_win/mpcli.exe` | `MPCLI_EXE=` |
| 固件 | `<repo>/bin/app.bin`（**无 MP 头**） | `FW=` |
| 下载口 | `COM13` | 位置参数 或 `PORT=` |
| 波特率 | `2000000` | `BAUD=` |
| 烧录地址 | `0x7009E000` | `ADDR=` |
| 完整参数 | `-p -A 0x7009E000 -b 2000000 -M 5 -r -u -d -T RTL87X3G` | — |

### 原理与注意事项

- 用 `wslpath -w` 把 WSL 侧固件路径转成 Windows 形式（`C:\...`）喂给 `-F`。
- 脚本会先检查 `mpcli.exe` 和 `bin/app.bin` 是否存在，缺任何一个就直接报错退出。
- `mpcli.exe`（frozen 打包）用 `sys.executable` 目录定位 `fw/` / `config/`，脚本 `cd` 到 exe 目录只是和 `tasks.json` 的 cwd 对齐，不影响功能。

### 常见问题

- **`固件不存在: .../bin/app.bin`**：先跑一次 `scripts/build.sh`。
- **`mpcli.exe 不存在`**：改 `MPCLI_EXE=` 指到你实际的安装位置。
- **端口占用 / 打不开 COM**：多半是 `serial_term.py` 还开着占用端口，先 `Ctrl+] → c` 关掉再 flash（或直接退串口终端）。

---

## 3. 查看 LOG

> ⚠️ **不要跑 `scripts/serial_term.py`**。本目录下这份只是**备份**，串口终端**已经在 Windows 侧长期跑着**，脚本本体和 log 目录都在 Windows 用户目录下。我们（包括自动化）只**读**它落下来的 log 文件，不重复启动脚本。

### 实际路径

| 视角 | 路径 |
|---|---|
| Windows（脚本本体） | `C:\Users\howie_wang.RSDOMAIN\serial_term.py` |
| Windows（log 目录） | `C:\Users\howie_wang.RSDOMAIN\log\` |
| **WSL 里读 log 用这个** | `/mnt/c/Users/howie_wang.RSDOMAIN/log/` |

文件命名规则：`<启动时间>_<端口>.log`，例如 `20260715_102606_COM14.log`（当前串口是 `COM14`，跟 `flash.sh` 默认的下载口 `COM13` 不是同一个口）。

### 最新一份 log

```bash
# 找最新
ls -t /mnt/c/Users/howie_wang.RSDOMAIN/log/*.log | head -1

# 直接跟随查看（tail -f 风格；ANSI 序列会被终端正确解释成颜色）
tail -f "$(ls -t /mnt/c/Users/howie_wang.RSDOMAIN/log/*.log | head -1)"
```

如果你只是想看某一次 boot / 某一次复现的输出，先按修改时间列一下再挑：

```bash
ls -lt /mnt/c/Users/howie_wang.RSDOMAIN/log/ | head
```

### 怎么看 log

Log 里存的是**芯片吐出来的原始字节**，包括 ANSI 转义（颜色、光标控制）。所以：

#### 推荐：能识别 ANSI 的工具

- **`less -R`**（WSL 首选）：
  ```bash
  less -R /mnt/c/Users/howie_wang.RSDOMAIN/log/20260715_102606_COM14.log
  ```
  `-R` 让 ANSI 序列按颜色渲染，而不是显示成 `^[[31m`。翻长 log 首选。
- **`cat`**：直接 `cat`，终端会解释 ANSI，效果和当时在 serial_term 里看到的一样。
- **VS Code + ANSI Colors 扩展**：Windows 侧直接开 `.log`，右键 → `ANSI Text: Open Preview`。

#### 不推荐

- **记事本 / 直接双击 `.log`**：ANSI 序列会显示成一堆 `←[0;32m` 这样的乱码，只有纯文本内容能看。
- **带自动换行的编辑器打开长行**：芯片有时会连续输出无换行的进度条更新（`\r` 覆盖），
  编辑器不认 `\r` 会显示成一大坨，`less -R` 才会正确处理。

#### 想去掉 ANSI 只留纯文本

```bash
# 剥 ANSI 转义（WSL / Git Bash 都能用）
sed -r 's/\x1b\[[0-9;]*[a-zA-Z]//g' \
  /mnt/c/Users/howie_wang.RSDOMAIN/log/20260715_102606_COM14.log > clean.log

# 或者用 ansi2txt（需安装 colorized-logs 包）
ansi2txt < /mnt/c/Users/howie_wang.RSDOMAIN/log/20260715_102606_COM14.log > clean.log
```

#### 想搜特定关键字

```bash
LOGDIR=/mnt/c/Users/howie_wang.RSDOMAIN/log

# 直接 grep 就行，ANSI 序列不会影响文本关键字匹配
grep -n "assert"    "$LOGDIR"/*.log

# 带上下文
grep -n -C3 "hardfault" "$LOGDIR"/*.log

# 只在最新那份里找
grep -n -C3 "hardfault" "$(ls -t "$LOGDIR"/*.log | head -1)"
```

### 备注

- **不要在 WSL 里再跑一份 `serial_term.py`**：COM 口在 Windows 端已经被那份进程占着了，WSL 里也开一份会抢串口、导致谁都收不全。
- **log 越攒越大？** `C:\Users\howie_wang.RSDOMAIN\log\` 里的文件永远不会被自动清理。要清就在 Windows 侧删，或者在 WSL 里 `rm /mnt/c/Users/howie_wang.RSDOMAIN/log/2025*.log`。
- **不确定 Windows 那份还在不在跑？** 看最新 log 的 mtime 有没有跟着串口输出在动就行：
  ```bash
  watch -n1 'ls -lt --time-style=+%H:%M:%S /mnt/c/Users/howie_wang.RSDOMAIN/log/ | head -3'
  ```
