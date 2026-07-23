# scripts

本目录下的常用工具脚本，覆盖 RTL87X3G 应用的**编译 → 下载 → 抓 log** 完整链路。所有脚本默认从**仓库根目录**跑也行、从 `scripts/` 里跑也行，路径都是相对 `scripts/` 自身解析的。

- `build.sh` —— `west build` 封装
- `flash-linux.sh` —— 原生 Linux 下用 Linux 版 `mpcli` 烧录（本机当前用这个）
- `flash-wsl.sh` —— WSL 里驱动 Windows 版 `mpcli.exe` 烧录（旧环境留档，Linux 下不用）
- `serial_term.py` —— **Windows 专用**串口终端（依赖 `msvcrt`，Linux 下跑不了）。Linux 下看 log 见「查看 LOG」章节，用现成命令即可

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
- 编译产物：`build/`，最终烧录用的固件在 `bin/app.bin`（由 `flash-linux.sh` 使用）。

### 常见问题

- **`west: command not found`**：先激活 Zephyr 环境（`source .venv/bin/activate` 或对应虚拟环境）。
- **改了 `prj.conf` / Kconfig 但没生效**：走 `scripts/build.sh -p` 触发一次 pristine。

---

## 2. 下载

用 `scripts/flash-linux.sh` 在原生 Linux 下直接调用 **Linux 版 `mpcli`**（原生 ELF，v4.0.0.7）。烧录参数对齐 `flash-wsl.sh`。

### 常用命令

```bash
scripts/flash-linux.sh                          # 默认 /dev/ttyUSB0
scripts/flash-linux.sh /dev/ttyUSB1             # 位置参数指定下载口
PORT=/dev/ttyUSB1 BAUD=3000000 scripts/flash-linux.sh  # 环境变量覆盖端口 / 波特率
DRY=1 scripts/flash-linux.sh                    # 只打印将要执行的命令,不真正烧录
```

### 默认配置

| 项 | 值 | 覆盖方式 |
|---|---|---|
| `mpcli` 路径 | `~/.local/mpcli/mpcli` | `MPCLI=` |
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

---

## 3. 查看 LOG

原生 Linux 下**不用** `serial_term.py`（它依赖 Windows 的 `msvcrt`，跑不了）。用系统现成命令即可：`stty` 配一次串口参数，`cat` 实时看、顺手 `tee` 存盘。

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
> 2. 监测起来后再按板上复位键 / 重新烧录（`flash-linux.sh` 结尾会 `Reboot`）；
> 3. 正在监听的进程就能完整抓到 `Booting Zephyr OS ...` 那一段。
> 顺序反了（先复位再起 cat）就只能抓到 boot 之后的静默，看着像"没输出"。

> 若装了 `picocom` / `tio` 也可以用（它们自带存盘、且能收发）：
> `picocom -b 2000000 --imap lfcrlf /dev/ttyUSB1` 或 `tio -b 2000000 /dev/ttyUSB1`。
> 注意 `picocom` 默认会拨 DTR/RTS，加 `--lower-rts --lower-dtr` 之类选项按需规避复位。

### 看 log ↔ 下载共用一个端口

`cat` / `picocom` 会占着串口，下载前先停掉：

1. 终端里 `cat "$PORT"` 看 log
2. `Ctrl+C` 停掉，释放端口
3. `scripts/flash-linux.sh` 下载固件
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
