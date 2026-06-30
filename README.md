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

| Tool | Min Version | Purpose |
|---|---|---|
| `python3` | ≥ 3.8 | west runtime |
| `west` | ≥ 1.2 | Workspace manager and custom commands |
| `git` | ≥ 2.20 | Version control |
| `arm-none-eabi-gcc` | ≥ 10.3 | Cross compiler (GCC builds) |
| `cmake` | ≥ 3.20 | Build system (GCC builds) |
| `ninja` | ≥ 1.10 | Parallel build (GCC builds) |
| `mpcli` | — | Firmware download tool |

> Keil MDK users do not need ARM GCC / CMake / Ninja — open the MDK project directly in the IDE.

## Component Repositories

| Repository | Local Path | Description |
| --- | --- | --- |
| [rtl87x3ep-hmi-sdk](https://gitee.com/realmcu/rtl87x3ep-hmi-sdk) | `honeycomb/` | Core SDK: HAL drivers, Bluetooth stack, system services, toolchain |
| [hmi-dashboard](https://gitee.com/realmcu/hmi/tree/rtl8773e-dashboard/) | `honeycomb/sdk/board/evb/hmi_dashboard/` | HMI application layer, BSP, GUI porting, build configuration |
| [HoneyGUI](https://gitee.com/realmcu/HoneyGUI) | `honeycomb/sdk/src/sample/gui/` | GUI engine: widget library, font engine, animations |
| [wearable](https://gitee.com/realmcu/wearable) | `honeycomb/sdk/src/app/Wearable/` | Wearable application layer code |
| [display](https://gitee.com/realmcu/display) | `honeycomb/sdk/src/mcu/display/` | LCD display driver library |

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
```

> `-m` must match the mode used at build time; a mismatch flashes the wrong OTA slot.

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

To invoke CMake directly, see [`gcc/README.md`](gcc/README.md).

### Keil MDK

Open `mdk/project.uvprojx` in Keil MDK 5 and build directly from the IDE.

If the project file needs to be regenerated (e.g. after configuration changes):

```bash
scons --target=mdk5
```

## Directory Structure

> Paths below are relative to `honeycomb/sdk/board/evb/hmi_dashboard/` (the main application repository root within the West workspace).

```
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
