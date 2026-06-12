# RTL87X3E HMI Dashboard

A firmware development package for the Realtek RTL87X3E SoC, targeting embedded HMI
applications with display and wireless connectivity.

## Features

| Feature | Status |
|---|---|
| HoneyGUI display engine | ✅ Available |
| Bluetooth (BLE + BR/EDR) | ✅ Available |
| OTA firmware update | ✅ Available |
| 4-mode build matrix (src/lib × bank0/bank1) | ✅ Available |
| Dual toolchain support (GCC / Keil MDK) | ✅ Available |

## Hardware

- **SoC**: Realtek RTL87X3E
- **Display**: ST7265, 800 × 480, RGB interface
- **Keys**: 8-key input (ADC-based)
- **Audio**: AW87390 smart PA (I2C)
- **Interfaces**: UART, I2C

## Prerequisites

| Tool | Purpose |
|---|---|
| `arm-none-eabi-gcc` | Cross compiler |
| `cmake` + `ninja` | Build system |
| `west` | Workspace manager and custom commands |
| `python3` | menuconfig scripts and west extensions |
| `mpcli` | Firmware download tool |

## Getting Started

### 1. Initialize the workspace

```bash
west init -l .manifest
west update
```

After the initial setup, use `west sync` instead of `west update` for day-to-day syncing.
It force-updates the manifest repo first, then runs `west update` and submodule updates.

### 2. Build the firmware

```bash
# Default: source mode, bank0 (OTA slot A)
west build

# Library mode, bank0 — links precompiled libgui.a (faster iteration)
west build -m lib_bank0
```

Other modes: `src_bank1` / `lib_bank1` (OTA slot B) — replace the `-m` value accordingly.

### 3. Flash the firmware

```bash
west flash            # default port COM3
west flash -p COM5    # specify a different port
```

## Build Command Reference

| Command | Description |
|---|---|
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

### CMake direct invocation (alternative)

Run from the `honeycomb/sdk/` directory:

```bash
# Default: source mode, bank0
cmake -G Ninja \
  -Dkconfig_path=board/evb/hmi_dashboard/gcc/defconfig.RTL8773E.hmi_dashboard_src_bank0 \
  -DIS_CHECK_FLOW=off \
  -Dcompile_lib_only=OFF \
  -B build
cmake --build build
```

For other modes, replace the suffix: `src_bank0` → `src_bank1` / `lib_bank0` / `lib_bank1`.

### Keil MDK

Open `mdk/project.uvprojx` in Keil MDK 5 and build directly from the IDE.

## Directory Structure

```
hmi_dashboard/
├── src/
│   ├── application/         # App entry point, feature flags, panel init
│   ├── bsp/                 # Startup code, system-level initialization
│   ├── gui_lib/             # HoneyGUI precompiled library and headers
│   ├── ports/
│   │   ├── realgui_port/    # HoneyGUI platform adaptation layer
│   │   └── lvgl_port/       # LVGL platform adaptation layer (in dev)
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
├── menu_config.h            # GUI menu configuration
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

### `src/ports/lvgl_port/` *(In Development)*

LVGL platform adaptation layer for display, input device, and filesystem.

### `src/hmi_rtk_bt/` *(In Development)*

Bluetooth stack integration covering:
- **BLE**: GAP, GATT profiles, and private GATT services
- **BR/EDR**: A2DP, AVRCP, HFP, SPP, PAN

### `src/protocol/` *(In Development)*

BLE private communication protocol implementation (L0 / L1 / L2 three-layer
architecture).

## Key Configuration Files

| File | Purpose |
|---|---|
| `app_flags.h` | Feature enable/disable switches for the whole application |
| `board.h` | GPIO pin mapping and peripheral configuration |
| `mem_config.h` | DTCM / ITCM memory region layout |
| `menu_config.h` | GUI menu settings |
| `gcc/defconfig.*` | Kconfig presets for GCC builds |
| `version.h` | Firmware version (`VERSION`, `BUILD_NUM`) |

## Version

The current firmware version is defined in `version.h`:

```c
#define VERSION     "3.14.8"
#define BUILD_NUM   72
```
