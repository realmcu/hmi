# Display Porting Guide

The RTL87X3E HMI Dashboard uses an ST7265, 800 × 480, RGB display by default. If the target has a different Driver IC, resolution, or interface, adapt the Panel Driver, Dashboard board configuration, and HoneyGUI Display Port.

If the resolution changes, update the Designer UI as well. See the [UI Project Porting Guide](ui-porting.md).

All paths in this document are relative to the West workspace root.

## 1. Determine the Required Changes

| Target display change | Panel Driver | `gui_port_dc.c` |
| --- | --- | --- |
| Different Driver IC; same interface and resolution | Select or add one | Usually unchanged |
| Different resolution; still RGB | Select or add one, then update timing | Check buffer sizes and hard-coded dimensions |
| RGB changed to SPI/QSPI | Select or add one | Refresh path must be adapted |
| SPI/QSPI changed to another serial panel | Select or add one | Check windowing, sections, and completion signaling |

> Keeping the same interface does not guarantee that the Display Port is compatible. Recheck `gui_port_dc.c` whenever the pixel format, stride, orientation, full/partial refresh behavior, or synchronization mechanism changes.

## 2. Collect the Display Parameters

Confirm the following from the schematic and panel datasheet before porting:

- Driver IC model;
- horizontal and vertical resolution;
- interface type: RGB, SPI, QSPI, and so on;
- input/output pixel format, such as RGB565 or RGB888;
- RGB pixel clock, HSYNC, VSYNC, DE polarity, and porch values;
- SPI/QSPI lane count, command format, maximum clock, and TE requirements;
- RESET, backlight, power, and interface pins;
- scan direction, rotation, and RGB/BGR order;
- full-refresh or partial-refresh requirements;
- framebuffer count and available PSRAM/SRAM;
- touch IC resolution, orientation, and coordinate mapping.

Bring up solid-color frames and validate refresh first. Add the complete UI only after the display path works, so Panel Driver failures and UI layout failures can be diagnosed independently.

## 3. Adapt the Panel Driver

The display repository and its RTL8773E driver directory are:

```text
sdk/src/mcu/display/
├── Kconfig
└── device/general/lcd/8773E/
    ├── CMakeLists.txt
    ├── SConscript
    ├── lcd_st7265_800480_rgb.c
    └── lcd_st7265_800480_rgb.h
```

### 3.1 The Target Driver IC Already Exists

1. Find the implementation matching both the target Driver IC and interface under `sdk/src/mcu/display/device/general/lcd/8773E/`. Do not select by IC name alone; the same IC may have variants for different resolutions or interfaces.
2. Find its `CONFIG_REALTEK_LCD_*` option in `sdk/src/mcu/display/Kconfig`.
3. Confirm that the option maps to the correct source file in both build files:
   - `sdk/src/mcu/display/device/general/lcd/8773E/CMakeLists.txt` for GCC;
   - `sdk/src/mcu/display/device/general/lcd/8773E/SConscript` for Keil MDK.
4. Disable the default ST7265 option and enable the target driver. Only one Panel Driver implementing the `rtk_lcd_hal_*` interface should be enabled in a build; otherwise duplicate symbols or the wrong implementation may be selected.
5. Check the pins, reset/power/backlight sequence, clock, timing, pixel format, and scan direction against the target hardware.

#### GCC Configuration

The four Dashboard modes use these defconfig files:

```text
sdk/board/evb/hmi_dashboard/gcc/
├── defconfig.RTL8773E.hmi_dashboard_src_bank0
├── defconfig.RTL8773E.hmi_dashboard_src_bank1
├── defconfig.RTL8773E.hmi_dashboard_lib_bank0
└── defconfig.RTL8773E.hmi_dashboard_lib_bank1
```

Edit the defconfig corresponding to the mode you build. The default configuration includes:

```ini
CONFIG_REALTEK_DISPLAY=y
CONFIG_REALTEK_DISPLAY_ENABLE_LCDC=y
CONFIG_REALTEK_LCD_ST7265_800_480_RGB=y
```

Set the default LCD option to `n` or remove it, then set the target driver's Kconfig option to `y`. Treat `sdk/src/mcu/display/Kconfig` and the generated `config.h` as the source of truth for configuration names.

#### Keil MDK Configuration

1. Open `sdk/board/evb/hmi_dashboard/menu_config.h` in Keil;
2. use **Configuration Wizard** to disable the default LCD driver and enable the target driver;
3. save and close the Keil project;
4. regenerate the project from `sdk/board/evb/hmi_dashboard/`:

```bash
scons --target=mdk5
```

5. reopen `mdk/project.uvprojx` and run *Rebuild all target files*.

If SCons is not used, the driver can also be switched directly in the Keil project:

1. Open *Project* → *Manage* → *Project Items* and locate the `lcd_low_driver` group;
2. remove the current Panel Driver `.c` file from that group and add the target driver's `.c` file;
3. under *Options for Target* → *C/C++ (AC6)*, make sure the target driver's header directory is in Include Paths;
4. disable the current driver macro and enable the target driver macro in `menu_config.h`;
5. perform a full Rebuild, then inspect the build log or map file to confirm that only the target Panel Driver is linked.

Manual edits affect only the current `mdk/project.uvprojx`. A later `scons --target=mdk5` regenerates the project from `menu_config.h` and `SConscript`, so keep those files consistent with the manual selection.

If the target driver is not listed in Configuration Wizard, add the corresponding option to `menu_config.h` and make sure `SConscript` uses exactly the same macro name.

### 3.2 The Target Driver IC Does Not Exist

Use an existing driver for the **same SoC and the same display interface** as the template. Matching the interface is more important than matching the resolution or vendor.

1. Add new `.c` and `.h` files under `sdk/src/mcu/display/device/general/lcd/8773E/`;
2. implement or update:
   - pinmux, power, reset, and backlight handling;
   - Panel initialization commands;
   - RGB/LCDC or SPI/QSPI controller configuration;
   - resolution, porch, clock, and signal polarity;
   - pixel format, RGB/BGR order, and scan direction;
   - full-screen or partial-window setup;
   - framebuffer update and transfer completion;
   - sleep, wake-up, and power on/off;
3. provide the common HAL entry points used by Dashboard:

```c
void rtk_lcd_hal_init(void);
void rtk_lcd_hal_update_framebuffer(uint8_t *buf, uint32_t len);
void rtk_lcd_hal_set_window(uint16_t x_start, uint16_t y_start,
                            uint16_t width, uint16_t height);
void rtk_lcd_hal_start_transfer(uint8_t *buf, uint32_t len);
void rtk_lcd_hal_transfer_done(void);
uint32_t rtk_lcd_hal_get_width(void);
uint32_t rtk_lcd_hal_get_height(void);
uint32_t rtk_lcd_hal_get_pixel_bits(void);
```

4. add a unique `CONFIG_REALTEK_LCD_*` option to `sdk/src/mcu/display/Kconfig`;
5. add the source to the RTL8773E `CMakeLists.txt` and `SConscript`, using the same configuration macro in both;
6. select the new driver as described in [The Target Driver IC Already Exists](#31-the-target-driver-ic-already-exists).

> The configuration macro must match character-for-character in Kconfig, CMake, and SCons. After registration, inspect the build log or map file to confirm that the intended source file was compiled and linked.

## 4. Update the Dashboard Board Selection

After the Panel Driver is included in the build, check the Dashboard-side selection as well:

- `sdk/board/evb/hmi_dashboard/src/application/app_gui.h`: `TARGET_LCD_DEVICE`, interface type, dimensions, and pixel format;
- `sdk/board/evb/hmi_dashboard/board.h`: LCD, touch, backlight, and other multiplexed pins;
- `sdk/board/evb/hmi_dashboard/src/application/main.c`: calls `rtk_lcd_hal_init()` during startup;
- `sdk/board/evb/hmi_dashboard/src/ports/realgui_port/gui_port_indev.c`: touch/key driver selection and coordinate source.

Add a clear, unique device definition to `app_gui.h` for a new panel. Enabling a display-repository option alone is not sufficient if Dashboard still handles the panel as the default ST7265. When the orientation or resolution changes, also update touch-axis swapping, mirroring, and coordinate ranges.

## 5. Adapt the HoneyGUI Display Port

The Display Port is located at:

```text
sdk/board/evb/hmi_dashboard/src/ports/realgui_port/gui_port_dc.c
```

The current implementation targets an 800 × 480 RGB panel. HoneyGUI renders a section of several rows, GDMA copies each section into one of two PSRAM framebuffers, and `rtk_lcd_hal_update_framebuffer()` switches the complete frame. The file also contains static buffers and PSRAM addresses calculated for 800 × 480 RGB565.

### 5.1 Keeping the RGB Interface

If the new driver keeps the same continuous RGB scan and double-framebuffer model, the refresh flow can usually be reused. Check all of the following:

- start address and spacing of both framebuffers;
- whether `width × height × bytes_per_pixel × framebuffer_count` fits the reserved PSRAM;
- section-buffer width, section height, and byte count;
- values returned by `rtk_lcd_hal_get_width()`, `rtk_lcd_hal_get_height()`, and `rtk_lcd_hal_get_pixel_bits()`;
- copy size for a final section shorter than a full section;
- cache, DMA address, width, and alignment restrictions.

Do not update only the Panel Driver dimensions while retaining fixed expressions such as `800 * 480 * 2` and `800 * 10 * 2` in `gui_port_dc.c`.

### 5.2 Changing from RGB to SPI/QSPI

The RGB framebuffer-switching path cannot be reused unchanged. SPI/QSPI panels generally need a command-mode refresh path in `gui_port_dc.c`:

1. derive `(x, y, width, height)` from the current section or dirty rectangle;
2. call `rtk_lcd_hal_set_window()` to select the Panel write region;
3. call `rtk_lcd_hal_start_transfer()` to send pixel data;
4. before reusing or overwriting a rendering buffer, wait through `rtk_lcd_hal_transfer_done()` or the driver's asynchronous completion mechanism;
5. handle TE, cache clean, DMA alignment, and maximum transfer length as required by the hardware;
6. reconfigure the `gui_dispdev` framebuffer, section, and callback fields.

Use the closest QSPI driver under `sdk/src/mcu/display/device/general/lcd/8773E/` and another SDK project with the same controller and HoneyGUI refresh model as references. Replacing only a header and Kconfig option is not sufficient.

## 6. Resolution and Memory

When the resolution changes, update or verify at least:

- width, height, and pixel bits returned by the Panel Driver;
- dimensions and section calculations in `app_gui.h`;
- framebuffer, section-buffer, and PSRAM addresses in `gui_port_dc.c`;
- available framebuffer range in the linker/memory configuration;
- touch coordinate range and orientation.

The minimum framebuffer capacity is:

```text
width × height × bits_per_pixel / 8 × buffer_count
```

For example, 800 × 480 RGB565 with two framebuffers requires 1,536,000 bytes, excluding section buffers, alignment, and other GUI resources.

> Changing the driver resolution does not scale the UI automatically. After adapting the display path, update the Designer project as described in the [UI Project Porting Guide](ui-porting.md).

## 7. Validation Checklist

- [ ] Exactly one Panel Driver is enabled in the build;
- [ ] RESET, power, and backlight sequences are correct;
- [ ] RGB solid colors render correctly;
- [ ] there is no corruption, tearing, or periodic flicker;
- [ ] Panel and HoneyGUI dimensions match;
- [ ] touch input works correctly;
- [ ] the framebuffer fits the reserved memory.
