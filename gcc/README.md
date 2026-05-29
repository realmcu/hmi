# HMI Dashboard GCC Build Guide

## Overview

HMI Dashboard supports GCC compilation with two build modes:

1. **Source Code Mode** - Build HoneyGUI from source code (GUI sub-repository)
2. **Library Mode** - Link with precompiled `libgui.a`

## Build Modes Comparison

| Feature | Source Code Mode | Library Mode |
|---------|-----------------|--------------|
| Build Time | Longer | Shorter |
| Flexibility | Can select demos | Fixed demo |
| Debug Support | Full source debug | Limited |
| Configuration | All Kconfig options | None |
| Required | GUI sub-repository | libgui.a |

## Build Commands

### Source Code Mode

```powershell
# In SDK root directory
cmake -G Ninja -D kconfig_path=board/evb/hmi_dashboard/gcc/defconfig.RTL8773E.hmi_dashboard_src -DIS_CHECK_FLOW=off -Dcompile_lib_only=OFF -B build
cmake --build build
```

### Library Mode

```powershell
# In SDK root directory
cmake -G Ninja -D kconfig_path=board/evb/hmi_dashboard/gcc/defconfig.RTL8773E.hmi_dashboard_lib -DIS_CHECK_FLOW=off -Dcompile_lib_only=OFF -B build
cmake --build build
```

## Demo Selection (Source Code Mode Only)

Edit `defconfig.RTL8773E.hmi_dashboard_src` to select different demos:

```ini
# Demo Selection - Uncomment ONE option
CONFIG_REALTEK_BUILD_EXAMPLE_IMAGE_WIDGET=y
# CONFIG_REALTEK_BUILD_EXAMPLE_SVG_WIDGET=y
# CONFIG_REALTEK_BUILD_EXAMPLE_GIF_WIDGET=y
# CONFIG_REALTEK_BUILD_EXAMPLE_TEXT_WIDGET=y
# CONFIG_REALTEK_BUILD_REAL_DOG_3D=y
# CONFIG_REALTEK_BUILD_GUI_800_480_DEMO=y
# ... more options in Kconfig.gui
```

## Output Files

After successful build, output files are located in:

```
board/evb/hmi_dashboard/bin/<config_name>/
├── honeygui_bank0.elf      # ELF file
├── honeygui_bank0.hex      # HEX file
├── honeygui_bank0.bin      # Binary file
├── honeygui_bank0_MP.bin   # Signed binary with header
├── honeygui_bank0.map      # Link map
└── honeygui_bank0.disasm   # Disassembly
```

## Feature Configuration

Available GUI features in `defconfig.RTL8773E.hmi_dashboard_src`:

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
