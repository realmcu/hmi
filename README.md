# RTL87X3E HMI Dashboard

A firmware development package for the Realtek RTL87X3E SoC, targeting embedded HMI
applications with display and wireless connectivity.

## Features

| Feature | Status |
| --- | --- |
| HoneyGUI display engine | ✅ Available |
| Bluetooth (BLE + BR/EDR) | ✅ Available |
| OTA firmware update | ✅ Available |
| 4-mode build matrix (src/lib × bank0/bank1) | ✅ Available |
| Dual toolchain support (GCC / Keil MDK) | ✅ Available |

## Prerequisites

| Tool | Min Version | Check Installed | Install / Download |
| --- | --- | --- | --- |
| `python3` | ≥ 3.8 | `python3 --version` | [python.org/downloads](https://www.python.org/downloads/) |
| `west` | ≥ 1.2 | `west --version` | `pip install west` |
| `git` | ≥ 2.20 | `git --version` | [git-scm.com/downloads](https://git-scm.com/downloads) |
| `arm-none-eabi-gcc` | ≥ 10.3 | `arm-none-eabi-gcc --version` | [Arm GNU Toolchain downloads](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads) |
| `cmake` | ≥ 3.20 | `cmake --version` | `pip install cmake` or [cmake.org/download](https://cmake.org/download/) |
| `ninja` | ≥ 1.10 | `ninja --version` | `pip install ninja` or [ninja releases](https://github.com/ninja-build/ninja/releases) |
| `scons` | 4.4.0 | `scons --version` | `pip install scons==4.4.0` |
| `kconfiglib` | - | `python3 -c "import kconfiglib"` | `pip install kconfiglib` |
| Keil MDK5 + ARM Compiler | ARM Compiler ≥ 6.22 | `armclang --version`, or Keil → *Help* → *About* | [keil.com/download/product](https://www.keil.com/download/product/) |

> - `arm-none-eabi-gcc` / `cmake` / `ninja` are only required for **GCC builds**. Keil users can skip them.
> - `scons` / `kconfiglib` are only needed when regenerating the Keil mdk5 project (see the
>   config-sync steps in the Keil MDK section below); not required for regular builds.
> - Keil users only need Keil MDK5 with **ARM Compiler ≥ 6.22**. An older/mismatched ARM Compiler
>   version is the most common cause of "the Keil project fails to build with a large number of errors" —
>   check *Project* → *Manage Project Items* → *Project Targets* → *Target* to confirm the compiler version
>   before filing a build issue.

## Getting Started

### 1. Initialize the workspace

```bash
# 1. Create a working directory (name is customizable)
mkdir hmi
cd hmi

# 2. Initialize the west workspace
west init -m https://gitee.com/realmcu/hmi-manifest.git --mr master --mf rtl8773e-dashboard-gitee.yml .

# 3. Sync all sub-projects
west update
```

> `west update` automatically clones every repository listed in [Component Repositories](#component-repositories)
> below — there is no need to manually download or `git clone` any of them.

### 2. Build the firmware

| Mode | Command | Build Time | Use Case |
| --- | --- | --- | --- |
| Source mode | `west build` or `west build -m src_bank0` | Slow (5–10 min first build) | When GUI source debugging is needed |
| Library mode | `west build -m lib_bank0` | Fast (~1 min) | Daily iteration (recommended) |

For OTA slot B, replace `bank0` with `bank1` (`src_bank1` / `lib_bank1`).

```bash
west build              # Default: source mode, bank0 (OTA slot A)
west build -m lib_bank0 # Library mode, bank0 (links precompiled libgui.a, faster)
```

### 3. Flash the firmware

```bash
west flash                  # default port COM3, flashes src_bank0 image
west flash -p COM5          # specify a different port
west flash -m src_bank1     # flash bank1 image (must match west build -m)
west userdata               # flash the designer UI's ROMFS resources, app untouched (see below)
```

> **The default port is COM3.** Use `-p COMx` for a one-off override; to change the
> default, edit `DEFAULT_COM` at the top of `west_commands_extention/commands.py`
> (affects west commands only).
>
> `-m` must match the mode used at build time; a mismatch flashes the wrong OTA slot.
>
> **UI not showing up?** The app image only contains program logic — the designer UI's
> images/fonts live in a separate ROMFS partition. Run `west userdata` once (and again
> whenever the UI resources change) to flash it. This command is standalone — it does
> **not** flash the app — and automatically prepends the RTL8773E MP header required by
> `src/application/designer/build/app_romfs.bin`, flashing it to the correct address
> without modifying the source file. A different bin can be packaged and flashed instead:
> `west userdata <path> [--addr <addr>]`. Add `--package-only` to just generate the
> header without touching the serial port.

## Build Command Reference

| Command | Description |
| --- | --- |
| `west build` | Default build (equivalent to `-m src_bank0`) |
| `west build -m lib_bank0` | Library mode, slot A (precompiled GUI, faster) |
| `west build -m src_bank1` | Source mode, slot B |
| `west build -m lib_bank1` | Library mode, slot B |
| `west build -c` | Clean then rebuild |
| `west build -j 8` | Set parallel job count |
| `west clean` | Remove build directory |
| `west clean --all` | Remove build directory and bin output |
| `west flash` | Flash via serial (default COM3, src_bank0) |
| `west flash -p <port>` | Flash via specified port |
| `west flash -m <mode>` | Flash image for specified mode (must match build) |
| `west size` | Show ELF section sizes |
| `west sync` | Force-update manifest repo, then `west update`, then submodule update |
| `west info` | Show workspace and build status |

To invoke CMake directly, see [`gcc/README.md`](gcc/README.md).

### Keil MDK

Open `sdk/board/evb/hmi_dashboard/mdk/project.uvprojx` in Keil MDK 5 and build
directly from the IDE.

If GUI/feature options need to change (e.g. switching demo, enabling a module), the
workflow is:

1. In the Keil *Project* window, double-click `menu_config.h` to open it, then switch to
   the **Configuration Wizard** tab at the bottom of the editor (this file is generated
   from that wizard — do not hand-edit the `#define` lines).
2. Toggle the desired checkboxes/options in the wizard, then save (`Ctrl+S`). Keil rewrites
   `menu_config.h` to match your selection.
3. Close the Keil project (so the file is not locked), open a terminal, and `cd` into
   `sdk/board/evb/hmi_dashboard/` (the same directory as this README and the
   `SConstruct` file).
4. Regenerate the Keil project so it picks up the new configuration:

   ```bash
   scons --target=mdk5
   ```

5. Re-open `sdk/board/evb/hmi_dashboard/mdk/project.uvprojx` in Keil MDK 5 and
   rebuild (*Project* → *Rebuild all target files*).

## Component Repositories

> These repositories are fetched automatically by `west update` in
> [Getting Started](#1-initialize-the-workspace) above — the links below are for reference
> only, you normally never need to clone them manually.

| Repository | Local Path | Description |
| --- | --- | --- |
| [rtl87x3ep-hmi-sdk](https://gitee.com/realmcu/rtl87x3ep-hmi-sdk) | `sdk/` | Core SDK: HAL drivers, Bluetooth stack, system services, toolchain |
| [hmi-dashboard](https://gitee.com/realmcu/hmi/tree/rtl8773e-dashboard/) | `sdk/board/evb/hmi_dashboard/` | HMI application layer, BSP, GUI porting, build configuration |
| [HoneyGUI](https://gitee.com/realmcu/HoneyGUI) | `sdk/src/sample/gui/` | GUI engine: widget library, font engine, animations |
| [wearable](https://gitee.com/realmcu/wearable) | `sdk/src/app/Wearable/` | Wearable application layer code |
| [display](https://gitee.com/realmcu/display) | `sdk/src/mcu/display/` | LCD display driver library |

## Chip-Specific Tools

The table above covers the open-source repositories managed alongside the SDK. In addition,
RTL87X3EP chip-specific tools (chip configuration, firmware download/flashing, OTA packaging,
etc.) are maintained in the [rtl87x3ep-mcu-hmi-sdk-tool](https://gitee.com/realmcu/rtl87x3ep-mcu-hmi-sdk-tool)
repository. It is not fetched automatically by `west update` — download it separately when needed.

| Tool | Purpose |
| --- | --- |
| `MCUConfigTool` | Chip / MCU configuration |
| `MPPGTool` | Firmware download (flashing), supports Watch devices |
| `CFUDownloadTool` | CFU firmware download |
| `DspConfigTool` | DSP configuration |
| `ImageConverter` | Image conversion |
| `DebugAnalyzer` | Debug analysis |
| `AciHostCLI` | ACI host command-line tool |
| OTA (Android / iOS) | OTA update package generation and test apps |
| AudioConnect (Android / iOS) | Audio connectivity test apps |

> The chip firmware package lives under `sdk/bin/` (e.g. `sdk/bin/rtl87x3ep/default_bin/`)
> and must be flashed using `MPPGTool`.

## Directory Structure

> Paths below are relative to `sdk/board/evb/hmi_dashboard/` (the main application repository root within the West workspace).

```text
hmi_dashboard/
├── src/
│   ├── application/         # App entry point, feature flags, panel init
│   ├── bsp/                 # Startup code, system-level initialization
│   ├── gui_lib/             # HoneyGUI precompiled library and headers
│   ├── ports/
│   │   └── realgui_port/    # HoneyGUI platform adaptation layer
│   ├── hmi_rtk_bt/          # Bluetooth stack integration (in dev)
│   └── protocol/            # BLE private protocol implementation (in dev)
├── gcc/                     # Linker scripts, defconfigs, build output
├── mdk/                     # Keil MDK project files
├── cfg/                     # Hardware configuration (rtl87x3ep)
├── inc/                     # Chip memory config headers
├── download/                # Firmware download tools
├── west_commands_extention/ # West custom command definitions
├── board.h                  # Pin mapping and peripheral configuration
├── mem_config.h             # Memory layout (DTCM1 / ITCM1)
├── menu_config.h             # GUI menu configuration
└── version.h                # Firmware version
```

## Module Overview

### `src/application/`

Application entry point (`main.c`), panel initialization, and the central feature
flag header (`app_flags.h`). Controls which subsystems are enabled at compile time.

### `src/bsp/`

Board support package: startup code for the RTL87X3 core and system-level
peripheral initialization.

### `src/gui_lib/`

HoneyGUI precompiled library (`libgui.a` for GCC, `gui.lib` for Keil) with the
corresponding public headers. Referenced in library build mode to speed up
compilation.

### `src/ports/realgui_port/`

Platform adaptation layer connecting HoneyGUI to the hardware: display controller,
input device, filesystem, OS interface, and flash translation layer.

### `src/hmi_rtk_bt/` *(In Development)*

Bluetooth stack integration covering:

- **BLE**: GAP, GATT profiles, and private GATT services
- **BR/EDR**: A2DP, AVRCP, HFP, SPP, PAN

### `src/protocol/` *(In Development)*

BLE private communication protocol implementation (L0 / L1 / L2 three-layer
architecture).

## Key Configuration Files

| File | Purpose |
| --- | --- |
| `app_flags.h` | Feature enable/disable switches for the whole application |
| `board.h` | GPIO pin mapping and peripheral configuration |
| `mem_config.h` | DTCM / ITCM memory region layout |
| `menu_config.h` | GUI menu settings |
| `gcc/defconfig.*` | Kconfig presets for GCC builds |
| `version.h` | Firmware version (`VERSION`, `BUILD_NUM`) |

## More Documentation

For official RTL8773E series datasheets, quick start guides, hardware notes, and SDK/GUI
online documentation, see the [RTL8773E-Series documentation center](https://www.realmcu.com/zh/Resources/Documentation/RTL8773E-Series#pagetab).
