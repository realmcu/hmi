# eBadge (RTL8773G + RTL8711)

Zephyr application project: HoneyGUI UI + OTA dual-bank firmware + FlashDB prebuilt wallpapers, board target
`rtl87x3g_evb`, with an external RTL8711 Wi-Fi module driven in SPI host mode.

- Build output: `bin/app.bin`, `bin/bank0/app_bank0.bin`, `bin/bank1/app_bank1.bin`
- Image with merged resources: `bin/bank<N>/app_bank<N>_packed.bin`
- romfs for standalone flashing: `bin/bank<N>/app_romfs_bank<N>_reloc.bin`
- FlashDB prebuilt resources: `tools/pack/fdb_resource_bin/fdb_prebuilt.bin`

## Directory layout

| Path | Description |
|---|---|
| [tools/scripts/](tools/scripts/) | Build / pack / flash scripts (`.sh` = WSL/Linux, `.ps1` = Windows, one-to-one equivalents) |
| [tools/pack/](tools/pack/) | Packing tool `pack.py` (app+romfs merge, FDB generation, layout checks) and `layout.json`; `fdb_resource_bin/` holds the FDB assets and products |
| [snippets/](snippets/) | Snippets owned by this project only: `wifi`, `flash_32M_bank0/1` |
| [app/designer/](app/designer/) | HoneyGUI Designer project (UI, assets, generated C code) |
| [port/](port/) | Board/middleware adaptation (display, flashdb, GUI port, etc.) |
| [component/](component/), [boards/](boards/), [dts/](dts/) | Components, board definitions and devicetree |
| `scripts/` | Separate repository (HoneyRepo/hmi) providing tools such as `scripts/tool/mpcli/mpcli.exe`; **not tracked by this repository** |

## Script overview

The two script sets are split by runtime environment and are **one-to-one equivalents**: `.sh` for WSL/Linux, `.ps1` for Windows.
Do not run `.sh` from PowerShell, and do not run `.ps1` inside WSL.
Every script derives the repository root from its own location, so it **can be run from any directory**, while output/intermediate directories stay under the repository root.

| Purpose | WSL / Linux | Windows PowerShell |
|---|---|---|
| Build (optionally pack / flash) | `./tools/scripts/build.sh` | `.\tools\scripts\build.ps1` |
| Generate the prebuilt wallpaper FDB image | `./tools/scripts/build_fdb.sh` | `.\tools\scripts\build_fdb.ps1` |
| Flash only the app image | `./tools/scripts/flash_app.sh` | `.\tools\scripts\flash_app.ps1` |
| Flash the FDB image | `./tools/scripts/flash_fdb.sh` | `.\tools\scripts\flash_fdb.ps1` |
| Flash only romfs | `./tools/scripts/flash_romfs.sh` | `.\tools\scripts\flash_romfs.ps1` |
| Layout check | `python3 tools/pack/pack.py check` | `python tools\pack\pack.py check` |

`pack.py` is the packing tool shared by both script sets (check / app / fdb); the scripts are thin wrappers around it.
Use `-h/--help` for the `.sh` scripts, and `Get-Help .\tools\scripts\build.ps1 -Detailed` for the `.ps1` ones.

## Environment setup

**WSL/Linux**

- west runs in a venv containing the Zephyr dependencies, by default `/home/sheldeng/Mi_eBadge/.venv/bin/python`
- Zephyr SDK, by default `/home/sheldeng/zephyr-sdk-0.16.9`
- Linux mpcli: `realtek-app/tools/mpcli/Linux/mpcli` (used by `build.sh --flash`)
- Flashing a Windows COM port needs WSLInterop or the `/init` fallback (the flash scripts handle this automatically)

**Windows**

- Python 3.12 (`build.ps1` uses it to run west and `pack.py`)
- Zephyr SDK (Windows version, containing `arm-zephyr-eabi-gcc.exe`). When neither
  `EBADGE_SDK_DIR` nor `ZEPHYR_SDK_INSTALL_DIR` is set, `build.ps1` tries
  `C:\zephyr-sdk-0.16.9`, `C:\zephyr-sdk-0.16.8`, `D:\zephyr-sdk-0.16.9`,
  `D:\zephyr-sdk-0.16.8` in turn; if none of them hits, it errors out and asks you to set the variable.
- **CMake and Ninja** (hard dependencies of `west build`). Reopen PowerShell after installing so PATH takes effect;
  `build.ps1` first checks whether `cmake` is visible.
- **Git Bash (`bash.exe`)**: Realtek's cmake uses it to run `version_gen_config.sh`
  (generating VERSION) and `md5_generate.sh` (post-build). The `bash.exe` shipped with Windows,
  `%LOCALAPPDATA%\Microsoft\WindowsApps\bash.exe`, is the WSL launcher and does not understand Windows
  paths such as `C:/...`, reporting `No such file or directory` (exit 127) — at best VERSION
  degrades to `0.0.0`, at worst the final md5 step fails the whole build. When `build.ps1` finds
  the WSL one on PATH, it automatically moves Git Bash's `bin` to the front.
- Windows mpcli: `scripts\tool\mpcli\mpcli.exe`
- **MinGW-w64 gcc or clang**: needed only by `build_fdb.ps1` (compiles FlashDB on the PC
  to generate the FDB image). Just put it on PATH, or point `CC` at the compiler path.

All paths are overridable, so a new machine needs no script edits:

| Variable | Script | Description |
|---|---|---|
| `VENV_PYTHON` / `SDK_DIR` / `BOARD` / `MPCLI` | build.sh | venv python, SDK, board name, mpcli path |
| `EBADGE_PYTHON` / `EBADGE_SDK_DIR` / `EBADGE_BOARD` / `EBADGE_MPCLI` / `EBADGE_PORT` | build.ps1 | the same, PowerShell version |
| `EBADGE_ROMFS` / `EBADGE_ROMFS_OFFSET` | build.ps1 | romfs input and fixed offset for `-Pack` |
| `ASSETS_DIR` / `OUT` / `WITH_VIDEO` | build_fdb.sh / build_fdb.ps1 | asset directory, output file, whether to include video |
| `CC` | build_fdb.sh / build_fdb.ps1 | host C compiler (Windows auto-detects gcc/clang) |
| `MPCLI_EXE` / `PORT` / `BAUD` / `DRY` | flash_*.sh | mpcli path, serial port, baud rate, print the command only |
| `FW` / `ADDR` | flash_app.sh | image file, flash address (derived from `--bank` by default) |
| `LAYOUT` / `FW` / `CHUNK_SIZE` | flash_fdb.sh | layout file, image, chunk size |

## Build

```bash
# default: OTA bank0 + wifi, incremental build -> bin/bank0/app_bank0.bin
./tools/scripts/build.sh

# clean rebuild (required the first time / after adding .c files)
./tools/scripts/build.sh --pristine

# bank1
./tools/scripts/build.sh --bank 1

# build both banks in order
./tools/scripts/build.sh --bank both --pristine

# plain development build with no bank/snippet -> bin/app.bin
./tools/scripts/build.sh --bank '' --snippet ''
```

Windows:

```powershell
.\tools\scripts\build.ps1 -Bank 0 -Snippet wifi -Pristine
.\tools\scripts\build.ps1 -Bank both -Pristine
```

> **Always go pristine after adding a `.c` file.** `app/spi_at_master/CMakeLists.txt` uses
> `file(GLOB)`, so incremental builds do not see new files and the symptom is an undefined symbol at link time.

Build directories are separated by snippet combination (`build_wifi/`, `build_bank0_wifi/`,
`build_bank1_wifi/`, ...), so switching combinations does not trigger Zephyr's snippet-change check.

## Packing (app + romfs)

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

`-Pack` / `--pack` appends romfs after app, 4KB-aligned, and iterates until `ROMFS_OFFSET` converges:

- Product: `bin/bank<N>/app_bank<N>_packed.bin` (the full image)
- Also: `bin/bank<N>/app_romfs_bank<N>_reloc.bin` (already relocated, can be flashed on its own)
- Without the MP header: `-NoMp` / `--no-mp`

See [tools/pack/README.md](tools/pack/README.md) for details.

## Flashing

**Full image (including romfs)** — flash directly after packing:

```bash
# WSL / Linux: Linux mpcli over /dev/ttyUSBx (usbipd attach first)
./tools/scripts/build.sh --bank 0 --flash --port /dev/ttyUSB0
```

```powershell
# Windows: COM port
.\tools\scripts\build.ps1 -Bank 0 -Pack -Flash -Port COM7
```

The flash address comes from `flash_map.ini` (currently bank0 = `0x7009E000`, bank1 = `0x704F3000`).

**App only** (leaves romfs / FDB alone):

```bash
# WSL: through the Windows mpcli.exe
./tools/scripts/flash_app.sh --bank 0            # default COM5
./tools/scripts/flash_app.sh COM7 --bank 1
./tools/scripts/flash_app.sh --fw bin/bank0/app_bank0_packed.bin
DRY=1 ./tools/scripts/flash_app.sh               # print the command only
```

```powershell
# Windows
.\tools\scripts\flash_app.ps1                    # bank0 + COM5
.\tools\scripts\flash_app.ps1 -Bank 1 -Port COM7
.\tools\scripts\flash_app.ps1 -Fw bin\bank0\app_bank0_packed.bin -DryRun
```

**UI assets only** (no app reflash after changing wallpapers, font sizes, etc.):

```bash
# WSL / Linux
./tools/scripts/build.sh --bank 0 --pack      # generate _reloc.bin first
./tools/scripts/flash_romfs.sh --bank 0       # default COM5
DRY=1 ./tools/scripts/flash_romfs.sh          # print the command only
```

```powershell
# Windows
.\tools\scripts\build.ps1 -Bank 0 -Pack
.\tools\scripts\flash_romfs.ps1 -Bank 0       # default COM5
.\tools\scripts\flash_romfs.ps1 -DryRun
```

The scripts verify that romfs is 4KB aligned and that the directory pointers point at this mount address, and refuse to flash if not.
**Do not** flash `app/designer/build/app_romfs_*.bin` directly — its addresses are the old absolute ones.

**Prebuilt wallpapers (FlashDB)**:

```bash
# WSL / Linux
./tools/scripts/build_fdb.sh                  # -> tools/pack/fdb_resource_bin/fdb_prebuilt.bin
./tools/scripts/flash_fdb.sh COM5             # flash to fdb.kvdb_addr from layout.json
```

```powershell
# Windows (generating the FDB needs MinGW gcc/clang, see "Environment setup")
.\tools\scripts\build_fdb.ps1
.\tools\scripts\flash_fdb.ps1                 # default COM5
.\tools\scripts\flash_fdb.ps1 -DryRun
```

Assets and products both default to `tools/pack/fdb_resource_bin/` (tracked with the project); when that directory has no
`wp-1.bin`, it falls back to the designer build tree. Use `--assets-dir` / `-AssetsDir`,
`--out` / `-Out`, or `ASSETS_DIR` / `OUT` to point at another location.
Before flashing, the image size and KVDB address are checked, along with whether app/OTA partitions would be overwritten.

> Under WSL the COM ports belong to Windows, so `mpcli.exe` must be used. `flash_*.sh` first tries to run
> the `.exe` directly (when WSLInterop is registered), otherwise falls back to `/init` + a temp `.bat`.
> On Windows, `flash_*.ps1` calls `mpcli.exe` directly and needs none of that.

## Layout checks

After changing the flash plan:

```bash
python3 tools/pack/pack.py check
```

It compares [tools/pack/layout.json](tools/pack/layout.json) against `flash_map.h`, the partition-table overlays,
`port/flashdb/fal_cfg.h` and the romfs mount code one by one, and reports only the mismatches.

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| undefined symbol at link time | not pristine after adding a `.c` file |
| partition table applied twice | `realtek-app/snippets/` also has a same-named `flash_32M_bank*`; delete that copy |
| all-black UI, garbled directory entries | romfs target and offset do not match; rerun `-Pack` / `--pack`; or something other than `_reloc.bin` was flashed |
| `already mounted` | `gui_vfs_mount_romfs()` is called repeatedly with the same prefix; use a different prefix |
| serial port cannot be opened | WSL: `usbipd attach` first; Windows: make sure the COM number is not in use |
| `mpcli` cannot find fw | it must be run from the mpcli directory (the scripts already do this) |
| PowerShell syntax errors / garbled Chinese | `.ps1` must be **UTF-8 with BOM** (the files in the repo are); do not save as ANSI/GBK |
| firmware still mounts the old address after editing `romfs_layout.h` | incremental builds do not rebuild the entry file; `-Pack`/`--pack` now detects this with `pack.py fwcheck` and touches/rebuilds automatically; for a manual build, touch `Mi_eBadgeEntry.c` yourself |
| `-Pack` builds but produces no `_packed.bin` | check whether the build phase failed; re-exporting from Designer moves `romfs_layout.h` from `src/` to `src/user/`, the scripts handle both locations, and old build artifacts should get one `-Pristine` |
| `-Pack` keeps repeating `rebuild bankN (4)/(5)` and finally gives up after 6 rounds | when `build_bank<N>` and `build_bank<N>_wifi` both exist, `pack.py fwcheck` picks the unused ELF and always reports stale. Fixed: it now defaults to `bin/<bank>/app_<bank>.elf` (produced together with the image), and `build.ps1` / `build.sh` also pass `--elf` explicitly |
| after alternating wifi and no-wifi builds, `-Pack` puts romfs into the app tail | `bin/bank<N>/` and `romfs_layout.h` are **shared** by all snippet combinations, while the two app sizes differ by about 34 KB. build and pack must use the same `-Snippet` combination; after switching, do not build without packing |
| `romfs would overlap app code; packing refused` (`pack.py` hard check) | `app_bank<N>.bin` and the firmware ELF are not from the same build — the offset was computed from the smaller one. Do not hand-edit `romfs_layout.h`; rerun `-Pack` / `--pack` with **the parameter set you actually intend to flash**, as the error message tells you; it recomputes the offset, touches the entry file and repacks, converging in 1-2 rounds |
| `build_fdb.ps1` cannot find a host C compiler | install MinGW-w64 or clang and add it to PATH, or set `CC=<gcc.exe path>` |
| `Zephyr SDK toolchain not found` | set `EBADGE_SDK_DIR` (or `ZEPHYR_SDK_INSTALL_DIR`) to the SDK root |
| `cmake is not on PATH: west build needs CMake` | install CMake + Ninja and reopen PowerShell (not a script problem) |
| `version_gen_config.sh: No such file or directory`, `exit 127`, the produced `_MP.bin` has version `0.0.0` | the `bash.exe` on PATH is the WSL launcher. `build.ps1` moves Git Bash to the front of PATH, but CMake caches `find_program` results in `CMakeCache.txt`, so **an old build directory needs one `-Pristine`** to re-probe (the script warns when it detects leftovers) |
| `.ps1` aborts with `NativeCommandError` as soon as it is `> log.txt` / `2>&1 \| Tee-Object` | PowerShell 5.1 wraps child stderr into an error, which together with `$ErrorActionPreference='Stop'` becomes a terminating error. `build_fdb.ps1` / `flash_*.ps1` already avoid this through `Invoke-Native` in `_common.ps1` (do the same elsewhere; do not call `& exe` directly) |
