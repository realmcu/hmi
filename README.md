# scripts

Common utility scripts in this directory, covering the full **build -> flash -> capture log** chain for RTL87X3G applications. The commands below assume they are run from the `applications/` directory, hence the `scripts/...` form; if you run them from the west workspace root, add the `applications/` prefix. Each script resolves paths relative to its own location and does not depend on the current directory at invocation.

- `build.sh` -- `west build` wrapper
- `flash-linux-app.sh` -- flash the App with the Linux build of `mpcli` on native Linux
- `flash-linux2win-app.sh` -- from a Linux workstation, drive the Windows PC connected to the board over SSH to flash the App
- `flash-wsl-app.sh` -- drive the Windows `mpcli.exe` from WSL2 to flash the App
- `flash-win32-app.bat` -- call `mpcli.exe` directly from a native Windows cmd to flash the App
- `flash-macos-app.sh` -- flash the App with the macOS build of `mpcli` on **macOS** (serial port auto-detection of `cu.*`), see "macOS native"
- `flash-runtime-linux.sh` / `flash-runtime-wsl.sh` / `flash-runtime-win32.bat` / `flash-macos-runtime.sh` -- flash the **whole runtime** (boot patch / upperstack / stack & sys patch / DSP / Config, 11 images in total), see "2.5 Flashing the runtime"
- `flash-linux2win-runtime.sh` -- from a Linux workstation, drive the Windows PC over SSH to flash the runtime
- `flash-wsl-romfs.sh` / `flash-linux2win-romfs.sh` / `flash-macos-romfs.sh` -- flash the HoneyGUI romfs resource image, see "2.6 Flashing romfs"
- `tool/mpcli/` -- the repo-bundled mpcli v4.0.0.7; `mpcli` (Linux ELF) and `mpcli.exe` (Windows) sit side by side and share `fw/` and `config/`. All flash scripts take it from here by default and no longer depend on an external install. The macOS build `mpcli-macos` (Mach-O, a build artifact not tracked in the repo) also lives in this directory, and the scripts use it by default with the same behavior as the other platform scripts
- `serial_term.py` -- a **Windows-only** serial terminal (depends on `msvcrt`, cannot run on Linux). To view logs on Linux, see the "Viewing LOG" section and use the built-in commands
- `generate_sdk.py` -- generate a trimmed SDK package from the release configuration in `SDK_COMMON.py` / `SDK_HMI.py`; see `python scripts/generate_sdk.py --help` for parameters

---

## 1. Build

Use `scripts/build.sh` to invoke `west build`; parameters match `.vscode/tasks.json`.

### Common commands

```bash
scripts/build.sh                        # incremental build (default board = rtl87x3g_evb)
scripts/build.sh -p                     # full rebuild (pristine, equivalent to --pristine)
BOARD=rtl87x3g_evb scripts/build.sh     # explicitly specify the board
DRY=1 scripts/build.sh                  # only print the command to run, do not build
```

### Notes

- Default board: `rtl87x3g_evb`, override with the `BOARD=` environment variable.
- Argument parsing only accepts `-p` / `--pristine`; any other unknown argument errors out immediately (to prevent slips).
- The script `cd`s to the repo root itself, so the current directory does not matter.
- Build output: `build/`; the firmware to flash is `bin/app.bin` (used by the App flashing scripts).

### Common issues

- **`west: command not found`**: activate the Zephyr environment first (`source .venv/bin/activate` or the corresponding virtual environment).
- **Changed `prj.conf` / Kconfig but it did not take effect**: run `scripts/build.sh -p` to trigger a pristine build.

---

## 2. Flash

### Linux -> Windows flashing (board connected to Windows)

For the "build on Linux, board connected to a Windows workstation" scenario. Linux logs in to Windows via an SSH alias; on first run it deploys `mpcli.exe`, the flash batch files and the firmware directory to `%USERPROFILE%\.rtl87-flash`, then uploads the App image and runs the flash remotely.

```bash
scripts/flash-linux2win-app.sh COM4
```

The default SSH host is `main-win-pc` and the COM port is `COM4`. Host, firmware, address and baud rate can be overridden:

```bash
WIN_HOST=my-win-pc scripts/flash-linux2win-app.sh COM8
FW=/path/to/app.bin scripts/flash-linux2win-app.sh COM8
ADDR=0x7009E000 BAUD=2000000 scripts/flash-linux2win-app.sh COM8
DRY=1 scripts/flash-linux2win-app.sh COM4
```

The Windows side needs OpenSSH Server enabled and non-interactive login configured for `ssh main-win-pc`. Runtime and romfs use `flash-linux2win-runtime.sh` and `flash-linux2win-romfs.sh` respectively; all three share the same Windows cache.

### Local flashing

Use `scripts/flash-linux-app.sh` on native Linux to call the **Linux build of `mpcli`** (native ELF, v4.0.0.7) directly. Flash parameters match `flash-wsl-app.sh`.

### Common commands

```bash
scripts/flash-linux-app.sh                          # default /dev/ttyUSB0
scripts/flash-linux-app.sh /dev/ttyUSB1             # positional argument to specify the download port
PORT=/dev/ttyUSB1 BAUD=3000000 scripts/flash-linux-app.sh  # environment variable overrides for port / baud rate
DRY=1 scripts/flash-linux-app.sh                    # only print the command to run, do not flash
```

### Default configuration

| Item | Value | Override |
|---|---|---|
| `mpcli` path | `scripts/tool/mpcli/mpcli` (repo-bundled v4.0.0.7) | `MPCLI=` |
| Firmware | `<repo>/bin/app.bin` (**no MP header**) | `FW=` |
| Download port | `/dev/ttyUSB0` | positional argument or `PORT=` |
| Baud rate | `2000000` | `BAUD=` |
| Flash address | `0x7009E000` | `ADDR=` |
| Full parameters | `-c <port> -T RTL87X3G -M 5 -p -A 0x7009E000 -F <fw> -b 2000000 -r -u -d` | — |

### How it works and notes

- Feed the Linux path directly to `-F`; no `wslpath` conversion needed.
- The script first checks that `mpcli`, `bin/app.bin` and the serial port exist and are writable; if any is missing it errors out.
- Accessing `/dev/ttyUSB*` requires the current user to be in the `dialout` group:
  ```bash
  sudo usermod -aG dialout "$USER"   # re-login (or newgrp dialout) is required after adding the group
  ```
- `mpcli` (frozen build) locates `fw/` / `config/` via the `sys.executable` directory; the script `cd`s to the mpcli directory only to keep behavior consistent, with no functional impact.

### Common issues

- **`firmware not found: .../bin/app.bin`**: run `scripts/build.sh` once first.
- **`mpcli not found or not executable`**: change `MPCLI=` to point to your actual install location.
- **`serial port not found` / `serial port not writable`**: check the USB connection (`ls /dev/ttyUSB*`), or add the user to the `dialout` group and re-login.
- **Port busy / cannot open**: most likely another serial tool (e.g. a manually started `cat /dev/ttyUSB*` or `picocom`) still holds the port; close it before flashing.

### Flashing under Windows / WSL

The parameter set of the WSL and native Windows scripts is identical to `flash-linux-app.sh` (`-p -A 0x7009E000 -b 2000000 -M 5 -r -u -d -T RTL87X3G`); only the execution environment differs. Both scripts use the repo-bundled `scripts/tool/mpcli/mpcli.exe` (v4.0.0.7) by default, with no extra installation needed.

#### Driving the Windows `mpcli.exe` from WSL2

```bash
scripts/flash-wsl-app.sh COM8                    # positional argument to specify the COM port (default COM13)
PORT=COM8 BAUD=2000000 scripts/flash-wsl-app.sh  # environment variable override
DRY=1 scripts/flash-wsl-app.sh COM8              # only print the command to run, do not flash
```

- Default `mpcli.exe` path: `scripts/tool/mpcli/mpcli.exe` (repo-bundled), override with `MPCLI_EXE=`.
- The firmware path is auto-converted to Windows form via `wslpath -w` and fed to `-F`.

#### Native Windows cmd (not entering WSL)

```bat
scripts\flash-win32-app.bat COM8              :: COM port is required, no default
set BAUD=3000000 && scripts\flash-win32-app.bat COM8
set DRY=1 && scripts\flash-win32-app.bat COM8       :: dry run, only print the command
set NOPAUSE=1 && scripts\flash-win32-app.bat COM8   :: no pause at the end (for scripts / tasks)
```

- Default `mpcli.exe` path: `scripts\tool\mpcli\mpcli.exe` (repo-bundled), override with `set MPCLI_EXE=`.
- Double-clicking also works: first `set PORT=COMx`, then double-click `flash-win32-app.bat`; it auto-`pause`s when done so you can read the output. When invoked from CI / tasks.json just set `NOPAUSE=1`.
- Overridable environment variables: `PORT` / `BAUD` / `FW` / `ADDR` / `MPCLI_EXE` / `DRY` / `NOPAUSE`.

#### macOS native

```bash
scripts/flash-macos-app.sh                     # auto-select when there is only one USB serial port
scripts/flash-macos-app.sh /dev/cu.usbserial-1420

# named parameters (recommended, no need to remember positional order)
scripts/flash-macos-app.sh --fw /path/app.bin --addr 0x7000A000 --port /dev/cu.usbserial-1420
scripts/flash-macos-app.sh --fw=/path/app.bin --addr=0x7000A000 --baud=3000000

# positional arguments are classified by content, any order: /dev/... is a serial port, 0x... is an address, the rest is firmware
scripts/flash-macos-app.sh /path/app.bin 0x7000A000 /dev/cu.usbserial-1420

# environment variable form is also supported (consistent with the Linux/WSL scripts)
PORT=/dev/cu.wchusbserial1420 BAUD=3000000 scripts/flash-macos-app.sh

DRY=1 scripts/flash-macos-app.sh               # only print the command to run, do not flash
scripts/flash-macos-app.sh --help              # print the script header usage
```

Priority: **command-line arguments > environment variables > defaults** (defaults `bin/app.bin` / `0x7009E000` / `2000000`).

Flash parameters are identical to the other platforms (`-p -A 0x7009E000 -b 2000000 -M 5 -r -u -d -T RTL87X3G`); only the execution environment differs. The three scripts `flash-macos-app.sh` / `flash-macos-runtime.sh` / `flash-macos-romfs.sh` correspond one-to-one to the Linux, WSL and win32 versions.

All three macOS scripts accept `--fw` / `--addr` / `--port` / `--baud` (short forms `-f` / `-a` / `-c` / `-b`, and also `--opt=value`); runtime additionally accepts `--chip`. Runtime is a fixed set of 11 images plus a fixed address table, and **deliberately rejects `--fw` / `--addr`**, telling you to use `flash-macos-app.sh` instead.

**Where mpcli-macos goes: the same directory as the other platforms' mpcli.** The three macOS scripts use `scripts/tool/mpcli/mpcli-macos` by default, alongside `mpcli` (Linux ELF) and `mpcli.exe` (Windows), with **no extra flag needed**, consistent with how `flash-linux-app.sh` takes `tool/mpcli/mpcli`. Use `MPCLI=` to override only when temporarily switching binaries:

```bash
MPCLI=$HOME/mpcli/mpcli scripts/flash-macos-app.sh
```

This Mach-O binary is not in the repo (it is a build artifact) and must be built from source -- the source is in `D:\RemotePrj\mpcli\` (with its own `scripts/build_mac.sh`); **PyInstaller cannot cross-compile, so it must run on macOS**:

```bash
# on macOS, run in the mpcli source directory
bash scripts/build_mac.sh          # produces dist/mpcli-<ver>-macos-<arch>.tar.gz

# unpack and place it in the tool directory, renaming it to distinguish it from the Linux ELF
tar -xzf dist/mpcli-*-macos-*.tar.gz -C /tmp
cp /tmp/mpcli-*-macos-*/mpcli <repo>/applications/eBadge_8773g_8711f/scripts/tool/mpcli/mpcli-macos
chmod +x <repo>/applications/eBadge_8773g_8711f/scripts/tool/mpcli/mpcli-macos
```

`build_mac.sh` produces a host-architecture single file (arm64 on Apple Silicon, x86_64 on Intel), with **no universal2**; build one per Mac architecture if you need both.

**`fw/` / `config/` need no separate preparation.** mpcli's `--flash-runtime` finds firmware via `mp_utility.get_root_path()`, which in the packaged build returns `sys._MEIPASS` -- i.e. the **binary's internal temporary unpack directory**. `mpcli_setup_mac.spec` already packs `./fw` and `./config` in, so `mpcli-macos` placed alone in `tool/mpcli/` can flash the runtime; the shared `fw/` in that directory is not required (keep it for Linux/Windows).

**The log directory must be writable.** `log.py` writes logs to `log/<date>/` **next to the executable**, and if that is not writable it crashes at startup:

```
PermissionError: [Errno 13] Permission denied: '/usr/local/bin/log'
```

(Verified in practice; the repo-bundled v4.0.0.7 behaves the same.) `tool/mpcli/` is inside the repo and normally writable, so the default location needs no handling. You only need to deal with this when you move mpcli to a read-only location such as `/usr/local/bin` or `/Applications` -- the three scripts fall back automatically by pointing `MPCLI_LOG_DIR` to `scripts/log/` (supported via `log.py:105` in the source, already included in v4.0.0.7), or you can set it explicitly yourself:

```bash
MPCLI_LOG_DIR=$HOME/.cache/mpcli scripts/flash-macos-app.sh
```

**Serial device names: use `cu.*`, not `tty.*`.** On macOS, `open()` on `/dev/tty.usbserial-*` waits for the DCD carrier, so when the chip is not up or not reset it hangs silently (looks like a freeze). The script switches a passed `tty.*` to the corresponding `cu.*` automatically; when none is passed it probes `/dev/cu.usbserial*`, `/dev/cu.wchusbserial*`, `/dev/cu.SLAB_USBtoUART*`, `/dev/cu.usbmodem*` -- if there is exactly one it uses it, otherwise it errors out and asks you to specify explicitly (to avoid flashing the wrong board).

> ⚠️ **`reboot_type=1` tries to open the "previous" serial port and fails on macOS.** mpcli's `registerM5For87x3G.__get_reboot_port()` splits the string on the `tty.usbserial` prefix and takes `-N` minus one, producing **`tty.usbserial-xxxx` (without the `/dev/` prefix)** -- a relative path that definitely cannot be opened. The default `config/chip_configs/RTL87X3G/custom_config.ini` has `is_reboot = False`, so by default it **does not take this path**; but if you set `is_reboot` to `True` for "auto reset before flashing", macOS will report `fail tot get debug board port` / `open ... fail`. To auto-reset on macOS, use `reboot_type = 0` (HCI reboot, via the same port given by `-c`) or `2` (CLI bud reboot), not `1`.

**macOS common issues**

- **`Bad CPU type in executable`**: running the x86_64 mpcli on Apple Silicon without Rosetta 2:
  ```bash
  softwareupdate --install-rosetta --agree-to-license
  ```
  The script checks with `lipo -archs` first and warns early.
- **`cannot be opened because the developer cannot be verified`** / Gatekeeper blocking: remove the quarantine attribute from the binary
  ```bash
  xattr -d com.apple.quarantine scripts/tool/mpcli/mpcli-macos
  ```
  If it reports `killed`: it is probably a signing issue; do a one-off local self-sign with `codesign --force --sign - scripts/tool/mpcli/mpcli-macos`.
- **`Resource temporarily unavailable` / port busy**: another serial tool (CoolTerm, minicom, `screen`, the Arduino IDE serial monitor) still holds the port; quit it first. After using `screen /dev/cu.usbserial-1420 2000000`, exit properly with `Ctrl+A K`; closing the terminal window directly leaves the port claimed.
- **`No such file or directory` but the device is present**: confirm the name with `ls /dev/cu.*`; CH340/CP2102/FTDI are named differently (`cu.wchusbserial*` / `cu.SLAB_USBtoUART*` / `cu.usbserial-*`). If you cannot find it, install the corresponding driver first (CH34x needs the vendor's latest driver on macOS 13+).
- **`PermissionError: ... log`**: mpcli is in a read-only directory, see "The log directory must be writable" above.
- **`firmware not found: .../bin/app.bin`**: run `scripts/build.sh` once first (building on macOS requires the appropriate toolchain; this repo's default flow still assumes Linux).

---

## 2.5 Flashing the runtime

Each App flashing script flashes only a **single App image** (`-p -A 0x7009E000 -F bin/app.bin`). When changing boards, refreshing the BT stack / DSP, or when the board is blank, you must first flash a **runtime**. This is handled by `flash-runtime-*` and `flash-linux2win-runtime.sh`, which internally call mpcli's `--flash-runtime` to generate `flash_image.json` and then flash everything.

### Common commands

```bash
scripts/flash-runtime-linux.sh                        # native Linux, default /dev/ttyUSB0
scripts/flash-runtime-linux.sh /dev/ttyUSB1           # positional argument to specify the download port
CHIP=RTL8783G scripts/flash-runtime-linux.sh          # switch chip
DRY=1 scripts/flash-runtime-linux.sh                  # dry run, view the command without a board

scripts/flash-runtime-wsl.sh COM8                     # WSL drives mpcli.exe (default COM13)
scripts/flash-linux2win-runtime.sh COM8               # Linux drives Windows over SSH to flash (default COM4)
CHIP=RTL8783G scripts/flash-linux2win-runtime.sh COM8
DRY=1 scripts/flash-linux2win-runtime.sh COM8         # only print the config, do not connect to Windows
```

```bat
scripts\flash-runtime-win32.bat COM8                  :: native Windows cmd, COM port is required
set CHIP=RTL8783G && scripts\flash-runtime-win32.bat COM8
```

```bash
scripts/flash-macos-runtime.sh                        # macOS, auto-detect the /dev/cu.* download port
scripts/flash-macos-runtime.sh --chip RTL8783G        # switch chip (also positional: <dev> <chip>)
CHIP=RTL8783G scripts/flash-macos-runtime.sh          # environment variable form
DRY=1 scripts/flash-macos-runtime.sh                  # dry run, view the command without a board
```

### Default configuration

| Item | Value | Override |
|---|---|---|
| Chip | `RTL8773G` (the directory name under `tool/mpcli/fw/runtime_system_bin/`; one of `RTL8773G` / `RTL8783G` / `RTL87X3EP`) | `CHIP=` |
| Download port | `/dev/ttyUSB0` (linux), `COM13` (wsl), `COM4` (linux2win), no default (win32), auto-detect `/dev/cu.*` (macOS) | positional argument or `PORT=` |
| Baud rate | `2000000` | `BAUD=` |
| Full parameters | `--flash-runtime <CHIP> -c <port> -u -d -b 2000000` | — |

`-T` need not be passed; mpcli derives it from `CHIP` (`RTL8773G` -> `RTL87X3G`).

`flash-linux2win-runtime.sh` is for the "build on Linux, board connected to Windows" scenario. Linux connects to `WIN_HOST` (default `main-win-pc`) over SSH; on first run it deploys `mpcli.exe`, the App/runtime batch files and the runtime firmware directory to Windows' `%USERPROFILE%\.rtl87-flash`, then runs `flash-runtime-win32.bat` remotely:

```bash
scripts/flash-linux2win-runtime.sh COM4
WIN_HOST=my-win-pc CHIP=RTL8783G scripts/flash-linux2win-runtime.sh COM8
```

This script shares the Windows cache with `flash-linux2win-app.sh` and `flash-linux2win-romfs.sh`. After flashing the runtime you still need to run `scripts/flash-linux2win-app.sh COM4` to flash the App.

### Notes

- **The runtime does not contain the APP**. The 11 image addresses are `0x70002000`(SYSTEM_Config) / `4000`+`7000`(boot patch0/1) / `a000`(upperstack) / `49000`(OTA header) / `4a000`(stack patch) / `7c000`(sys patch) / `21e000`+`2bc000`+`2fc000`(DSP sys/app/cfg) / `306000`(APP_Config), **with no APP at `0x7009E000`**. A full flash takes two steps:
  ```bash
  scripts/flash-runtime-linux.sh          # 1. flash the runtime first
  scripts/flash-linux-app.sh              # 2. then flash bin/app.bin
  ```
- **`-u -d` are added explicitly by the script; do not remove them**. mpcli's `--flash-runtime` only injects `-M 5 -r -b 3000000`, and `SYSTEM_Config` lands in the protected `0x70002000` (OEM_CFG region), which cannot be written without `-u`.
- `--flash-runtime` **does not support** `--gen-output-dir`; the generated `flash_image.json` / `flash_image_report.txt` inevitably go to `fw/runtime_system_bin/<CHIP>/`. They are ignored in `tool/mpcli/.gitignore` and will not pollute the repo.
- The WSL version reads firmware over a UNC path (`\\wsl.localhost\...`), slower than native Linux (scanning 11 bins takes ~2.5s vs 0.03s). If that is too slow, copy `fw/runtime_system_bin/<CHIP>` to a Windows drive and use the two-step approach:
  ```bash
  cd scripts/tool/mpcli
  ./mpcli.exe --gen-json --bin-dir <firmware directory on a Windows drive> --gen-output-dir <output directory>
  ./mpcli.exe -f <output directory>\flash_image.json -a -c COM8 -T RTL87X3G -b 2000000 -M 5 -r -u -d
  ```

### Flashing other firmware directories / a single image

- **Flashing SDK build output or a third-party firmware package** (the directory must contain `.bin` files with an MP header and `flash_map.ini`):
  ```bash
  cd scripts/tool/mpcli
  ./mpcli --gen-json --bin-dir <firmware directory> -T RTL87X3G -c /dev/ttyUSB0 -u -d -b 2000000
  ```
- **Flashing a single image**: the existing `flash-*` scripts' `ADDR=` / `FW=` suffice; look up the address in the list above or in `fw/runtime_system_bin/<CHIP>/flash_map.ini`:
  ```bash
  ADDR=0x7000A000 FW=/path/to/upperstack.bin scripts/flash-linux-app.sh
  ```

---

## 2.6 Flashing romfs

The HoneyGUI resource image is a bare image independent of the App; its flash address must agree with `romfsBaseAddr` in `ui/project.json`, the `gui_vfs_mount_romfs()` call and the partition table address. The address must be 4 KB aligned.

### WSL2

```bash
scripts/flash-wsl-romfs.sh ../ui/build/app_romfs_0x704D1000.bin 0x704D1000 COM8
```

### Linux -> Windows

```bash
scripts/flash-linux2win-romfs.sh ../ui/build/app_romfs_0x704D1000.bin 0x704D1000 COM4
WIN_HOST=my-win-pc DRY=1 scripts/flash-linux2win-romfs.sh
```

### macOS

```bash
scripts/flash-macos-romfs.sh ../ui/build/app_romfs_0x70536000.bin              # address derived from the file name
scripts/flash-macos-romfs.sh ../ui/build/app_romfs_0x70536000.bin 0x70536000 /dev/cu.usbserial-1420
scripts/flash-macos-romfs.sh --fw ../ui/build/app_romfs_0x70536000.bin --port /dev/cu.usbserial-1420
DRY=1 scripts/flash-macos-romfs.sh ../ui/build/app_romfs_0x70536000.bin
```

> The partition address must follow this App's `flash_map.h`: `USER_DATA1_ADDR = 0x70536000` (partition tables differ between App directories; the `0x704D1000` above is an old value from another project -- **run `grep USER_DATA1_ADDR flash_map.h` to confirm before flashing**).

The script checks address alignment, the base address baked into the image and the romfs root dirent. The resource image uses a different flash offset from the App and runtime.

---

## 3. Viewing LOG

On native Linux you **do not** use `serial_term.py` (it depends on Windows' `msvcrt` and cannot run). Just use the built-in commands: configure the serial parameters once with `stty`, watch in real time with `cat`, and save to disk with `tee`.

> **Viewing the log on macOS**: there is no `stty -F` (BSD uses `stty -f`), and the serial port is `/dev/cu.*`:
> ```bash
> PORT=/dev/cu.usbserial-1420; BAUD=2000000
> stty -f "$PORT" "$BAUD" cs8 -cstopb -parenb -crtscts clocal -hupcl raw -echo
> stdbuf -oL cat "$PORT" | tee scripts/log/$(date +%Y%m%d_%H%M%S).log   # stdbuf requires brew install coreutils
> ```
> Without `stdbuf`, use `cat "$PORT" | tee -a file` (macOS `cat` is also fully buffered when writing to a pipe, so the boot log gets stuck and never hits disk), or `script -q /dev/null cat "$PORT"`, `picocom -b 2000000 /dev/cu.usbserial-1420`, `tio -b 2000000 /dev/cu.usbserial-1420`.

> ⚠️ **RTS/DTR and reset**: RTL87X3G's RTS is an **active-low reset line**. Many serial tools assert RTS/DTR when opening the port, holding the chip in reset. The commands below use `stty` to disable hardware flow control (`-crtscts`) and `hupcl`, avoiding an accidental reset when opening/closing the port. If your USB-to-serial driver still toggles DTR/RTS on open, seeing the chip reset is normal; just reconnect.

### Configure the serial parameters once

```bash
PORT=/dev/ttyUSB1        # the port for viewing the log (the download port is usually ttyUSB0; the log port is another one)
BAUD=2000000

# 8N1, no flow control, raw, no hangup reset when opening/closing
stty -F "$PORT" "$BAUD" cs8 -cstopb -parenb -crtscts -hupcl raw -echo
```

### View the log in real time (can also save to disk)

```bash
# view only (direct output to the terminal) -- the terminal is line buffered, flushes in real time, and renders ANSI colors
cat "$PORT"
```

⚠️ **Do not use `cat "$PORT" | tee file` or `cat "$PORT" > file` to save** -- once `cat`'s
stdout is not a terminal (a pipe or a file), glibc switches it to **4KB full buffering**, and it
will not hit disk until 4KB accumulates. A boot log is often only a few hundred bytes to 1KB, so you will see "the file stays at 0 bytes /
`tail -f` shows nothing" while the data is actually stuck in the `cat` process's memory buffer, never written. This pitfall has been hit in practice.

**Saving requires forced line buffering or no buffering**; use one of the following:

```bash
mkdir -p scripts/log
LOG="scripts/log/$(date +%Y%m%d_%H%M%S)_${PORT//\//_}.log"

# Option A (recommended): stdbuf forces line buffering, watch and save at once
stdbuf -oL cat "$PORT" | tee "$LOG"

# Option B: dd writes each byte straight to the file, absolutely unbuffered (save only; watch with tail -f in another terminal)
dd if="$PORT" of="$LOG" bs=1
```

`Ctrl+C` stops it. The `${PORT//\//_}` in the file name turns `/dev/ttyUSB1` into `_dev_ttyUSB1`.

> **The right way to capture the boot log**: boot messages are printed only at the instant of power-on/reset and are gone if missed.
> So **start monitoring first, then reset**:
> 1. First run `stdbuf -oL cat "$PORT" | tee "$LOG"` in a terminal (or `dd ...`);
> 2. After monitoring is up, press the board's reset button / re-flash (the App flashing scripts run `Reboot` at the end);
> 3. The listening process then captures the entire `Booting Zephyr OS ...` segment.
> If the order is reversed (reset first, then start cat), you only capture the silence after boot, which looks like "no output".

> If `picocom` / `tio` are installed you can use those too (they save to disk and can send/receive):
> `picocom -b 2000000 --imap lfcrlf /dev/ttyUSB1` or `tio -b 2000000 /dev/ttyUSB1`.
> Note that `picocom` toggles DTR/RTS by default; add options such as `--lower-rts --lower-dtr` to avoid the reset as needed.

### Sharing one port between viewing the log and flashing

`cat` / `picocom` hold the serial port; stop them before flashing:

1. In the terminal, `cat "$PORT"` to view the log
2. Stop it with `Ctrl+C` to release the port
3. `scripts/flash-linux-app.sh` to flash the firmware
4. `cat "$PORT"` again to resume viewing

### Viewing saved logs

A log contains the **raw bytes the chip emitted**, including ANSI escapes (colors, cursor control).

#### Recommended: tools that understand ANSI

- **`less -R`** (the go-to for paging long logs):
  ```bash
  less -R scripts/log/20260717_160000__dev_ttyUSB1.log
  ```
  `-R` renders ANSI sequences as colors instead of showing `^[[31m`.
- **`cat`**: just `cat` the file; the terminal interprets ANSI.
- **`tail -f`**: follow the latest one:
  ```bash
  tail -f "$(ls -t scripts/log/*.log | head -1)"
  ```

#### Stripping ANSI to leave plain text

```bash
# strip ANSI escapes
sed -r 's/\x1b\[[0-9;]*[a-zA-Z]//g' scripts/log/20260717_160000__dev_ttyUSB1.log > clean.log

# or use ansi2txt (requires the colorized-logs package: sudo apt install colorized-logs)
ansi2txt < scripts/log/20260717_160000__dev_ttyUSB1.log > clean.log
```

#### Searching for a specific keyword

```bash
LOGDIR=scripts/log

grep -n "assert"        "$LOGDIR"/*.log       # plain grep; ANSI does not affect text matching
grep -n -C3 "hardfault" "$LOGDIR"/*.log       # with context
grep -n -C3 "hardfault" "$(ls -t "$LOGDIR"/*.log | head -1)"  # search only the latest one
```

### Notes

- **Port permissions**: reading `/dev/ttyUSB*` also requires the `dialout` group (see the "Flash" section).
- **"No output" troubleshooting order**:
  1. **Saved file is 0 bytes / `tail -f` is empty** -> almost certainly the full-buffering pitfall of `cat | tee` / `cat > file`;
     switch to `stdbuf -oL cat` or `dd bs=1` (see above). First verify the channel by sending a command:
     `printf 'kernel version\r\n' > "$PORT"`, which normally replies `Zephyr version 3.7.2`.
  2. **Even `cat "$PORT"` straight to the terminal shows nothing** -> the device is not printing on its own right now; press the board's reset button,
     or trigger an operation that prints (the boot log is printed only at the reset instant).
  3. **Garbled output** -> the `stty` baud rate does not match the firmware; make sure both are `2000000`.
- **Logs keep growing**: `scripts/log/` is not cleaned automatically; `rm scripts/log/2026*.log` as needed.
