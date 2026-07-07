# HMI Dashboard GCC Build Guide

## Overview

HMI Dashboard supports GCC compilation with a 2×2 build mode matrix: GUI source (`src`) or precompiled library (`lib`), combined with OTA slot A (`bank0`) or B (`bank1`).

## Build Mode Matrix

| Feature | Source Mode (src) | Library Mode (lib) |
|---------|-------------------|--------------------|
| Build Time | Longer (5-10 min first run) | Shorter (~1 min) |
| Debug Support | Full source debug | Limited |
| Demo Selection | Switchable | Fixed |
| Required | GUI sub-repository | libgui.a (provided) |

## Build Commands

Run from the SDK root directory (`sdk/`):

```powershell
# Default: source mode, bank0
cmake -G Ninja -D kconfig_path=board/evb/hmi_dashboard/gcc/defconfig.RTL8773E.hmi_dashboard_src_bank0 -DIS_CHECK_FLOW=off -Dcompile_lib_only=OFF -B build
cmake --build build
```

For other modes, replace the suffix: `src_bank0` → `src_bank1` / `lib_bank0` / `lib_bank1`.

> **Tip**: Use `lib_bank0` for daily iteration; switch to `src_bank0` only when debugging GUI internals.

## Demo Selection (Source Mode Only)

Edit `defconfig.RTL8773E.hmi_dashboard_src_bank0` to select a demo (only one at a time):

> `src_bank1` has the same demo options — it differs only by `CONFIG_REALTEK_COMPILE_BANK1=y`.

```ini
# 2D Graphics
CONFIG_REALTEK_BUILD_EXAMPLE_IMAGE_WIDGET=y
# CONFIG_REALTEK_BUILD_EXAMPLE_SVG_WIDGET=y
# CONFIG_REALTEK_BUILD_EXAMPLE_GIF_WIDGET=y
# CONFIG_REALTEK_BUILD_EXAMPLE_TEXT_WIDGET=y

# 3D Graphics
# CONFIG_REALTEK_BUILD_REAL_DOG_3D=y
# CONFIG_REALTEK_BUILD_REAL_EARTH_3D=y
# CONFIG_REALTEK_BUILD_REAL_DONUT_3D=y

# Screen Fit Demos
# CONFIG_REALTEK_BUILD_GUI_800_480_DEMO=y
# CONFIG_REALTEK_BUILD_GUI_466_466_DEMO=y
# CONFIG_REALTEK_BUILD_GUI_240_240_DEMO=y

# HML Designer
# CONFIG_REALTEK_BUILD_HML_DESIGNER=y

# ... more options in Kconfig.gui
```

## Output Files

Build outputs are isolated per mode under `board/evb/hmi_dashboard/bin/RTL8773E.hmi_dashboard_<mode>/`:

```
# Example: bin/RTL8773E.hmi_dashboard_src_bank0/
honeygui_src.elf              # ELF file
dashboard_bank0_MP-*.bin      # Signed binary for flashing

# bank1 modes (src_bank1 / lib_bank1):
dashboard_bank1_MP-*.bin      # Signed binary for flashing
```

## Feature Configuration

Available GUI features in `defconfig.RTL8773E.hmi_dashboard_src_bank0`:

```ini
# 3D Graphics
CONFIG_REALTEK_BUILD_LITE3D=y

# Physics Engine
CONFIG_REALTEK_BUILD_GUI_BOX2D=n

# Particle System
CONFIG_REALTEK_BUILD_PARTICLE_SYSTEM=n

# HML Designer Support
CONFIG_REALTEK_BUILD_XML_LOADER=n

# Debug Tools
CONFIG_REALTEK_BUILD_LETTER_SHELL=y
CONFIG_REALTEK_BUILD_MONKEY_TEST=n
```

## Device Configuration

```ini
# Key Button
CONFIG_REALTEK_KEY_BUTTON_8773E=y

# LCD Display
CONFIG_REALTEK_LCD_ST7265_800480_RGB=y

# ROMFS Support
CONFIG_REALTEK_ROMFS=y
```

## Comparison with MDK

| Aspect | MDK | GCC |
|--------|-----|-----|
| Config Tool | menu_config.h | defconfig + Kconfig |
| Build Tool | scons | cmake |
| Source/Library | menuconfig option | defconfig selection |
| Demo Select | menu_config.h macro | Kconfig choice |

## Troubleshooting

### Compiler not found

Make sure the Arm GNU Toolchain is on `PATH`:

```powershell
arm-none-eabi-gcc --version
# expected: Arm GNU Toolchain 12.3.Rel1 ... 12.3.1
```

Recommended path: `C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\12.3 rel1\bin`

### Missing GUI source code

If building in source code mode fails with missing source files:

```bash
# Ensure GUI sub-repository is available
ls src/sample/gui/
```

### Library not found

If building in library mode fails:

```bash
# Ensure libgui.a exists
ls board/evb/hmi_dashboard/src/gui_lib/gcc/libgui.a
```

### Build errors

1. Clean build directory and retry
2. Check defconfig syntax
3. Verify toolchain installation

```bash
arm-none-eabi-gcc --version
cmake --version
```
