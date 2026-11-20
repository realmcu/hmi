# mpcli — MP Tool CLI for Realtek RTL87xx

mpcli is the mass-production flashing CLI tool for Realtek RTL87xx series chips, supporting flash image programming over UART.

## New features in v4.0.0.7

### 1. `--gen-json` — auto-generate flash_image.json from MP headers

Adds the `--gen-json` sub-mode, which generates `flash_image.json` from an SDK build output directory without connecting a device:

```bash
# basic usage (no -T needed)
mpcli --gen-json --bin-dir <SDK_OUTPUT_DIR>
```

The tool scans all `.bin` files, reads their **MP header** (written by `prepend_header.exe`), and extracts the flash address and info from it, with no hard-coded slot table or file-name pattern matching.

The generated `flash_image.json` is placed directly in the SDK output directory and can then be flashed via `-a` mode.

### 2. How MP header driven operation works

Every SDK output `.bin` file starts with a **512-byte MP header** (TLV format) containing:

| ITEM ID | Field | Description |
|---------|------|-------------|
| `0x0001` | BIN_ID | file type identifier (APP=0x0300, OTA_HEADER=0x0800, etc.) |
| `0x001C` | **Download address** | target flash address (BANK0/BANK1 already distinguished) |
| `0x0004` | Data length | actual content size (excluding header) |

Address resolution order:
1. **ITEM 0x001C**: the download address in the MP header (preferred, already contains BANK info)
2. **BIN_ID + flash_map.ini**: files without ITEM 0x001C are resolved via BIN_ID + directory path + flash_map.ini

### 3. Dual-Bank support (BANK0 + BANK1)

BANK0 and BANK1 images of dual-Bank devices are auto-recognized into a single JSON; single-Bank devices are unaffected.

Shared files such as `APP_Config` automatically get two address entries (BANK0 + BANK1).

### 4. No port/baud in the JSON

`flash_image.json` no longer stores serial parameters. The port and baud rate are given on the command line when flashing:

```bash
mpcli -f flash_image.json -a -c COM3 -b 3000000 -M 5 -r -u -d -T RTL87X3EP
```

Compatibility: older JSON (with port/baud) still parses correctly.

## Full workflow

### Option 1: generate and flash with a single command (recommended)

When `--gen-json` is combined with `--bin-dir`, `-T` and `-c`, it generates `flash_image.json` and then **flashes directly** in one step (auto-injects the defaults `-M 5 -r -b 3000000`; `-b` can override explicitly; `-u`/`-d` are on by default):

```bash
# Linux (running from source)
python3 __main__.py --gen-json --bin-dir <firmware directory> -T RTL87X3G -c /dev/ttyUSB0

# Linux (compiled build, firmware bundled under fw/runtime_system_bin/<chip>/)
/home/wh/.local/mpcli/mpcli --gen-json \
    --bin-dir /home/wh/.local/mpcli/fw/runtime_system_bin/RTL8783G \
    -T RTL87X3G -c /dev/ttyUSB0

# Windows
mpcli --gen-json --bin-dir "C:\SDK_Build\output" -T RTL87X3EP -c COM3
```

> `-T` mapping: RTL8773G / RTL8783G → `RTL87X3G`; RTL87X3EP → `RTL87X3EP`.
> **When `-c` is not given** (missing `-c` or `-T`) it only generates the JSON and exits without flashing (see step 1 of Option 2).

### Option 2: two steps (separate generation / flashing)

```bash
# 1. generate flash_image.json (no device needed)
mpcli --gen-json --bin-dir "C:\SDK_Build\output"

# 2. flash all images (device required)
mpcli -f "C:\SDK_Build\output\flash_image.json" -a -c COM3 -b 3000000 -M 5 -r -u -d -T RTL87X3EP

# 3. if the APP is not in the SDK directory, flash it separately
mpcli -c COM3 -p -A 0x02098000 -F app.bin -b 3000000 -M 5 -r -u -d -T RTL87X3EP
```

## Output files

| File | Description |
|------|-------------|
| `flash_image.json` | mpcli JSON config file (input for -a mode) |
| `flash_image_report.txt` | flash report (image list, size info) |

## Command-line parameters

| Parameter | Description |
|-----------|-------------|
| `-T` | IC type: RTL87X3E / RTL87X3EP / RTL87X3D / RTL87X3G / RTL87X3J (not needed with --gen-json) |
| `-c` | COM port, e.g. COM3 |
| `-b` | override baud rate (flashing speed) |
| `-B` | open baud rate (initial connection speed) |
| `-M` | flash mode, 5 = MP Loader mode |
| `-a` | flash all images in the JSON |
| `-f` | JSON config file path |
| `-r` | reboot after flashing |
| `-u` | unlock protected regions |
| `-d` | disable OEM config merge |

### --gen-json specific parameters

| Parameter | Description |
|-----------|-------------|
| `--gen-json` | enable JSON generation mode; if `--bin-dir`+`-T`+`-c` are also given, flash directly after generation (one step) |
| `--bin-dir` | SDK build output directory (scans the MP header of all *.bin files) |
| `--gen-output-dir` | output directory (default = --bin-dir) |
| `--gen-verbose` | print the MP header parse info of each file |

> APP auto-discovery: if `app_*.bin` (in MP header format) exists in the SDK directory, it is integrated into the JSON automatically; otherwise it is SKIPped and can be flashed separately with `-p -A -F`.

## Building mpcli.exe

```bash
pip install pyinstaller
pyinstaller mpcli_setup_win.spec
```

The generated file is at `dist/mpcli.exe`.
