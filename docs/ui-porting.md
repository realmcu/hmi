# UI Project Porting Guide

The Dashboard SDK includes a default Dashboard Designer UI. Follow this guide to modify that UI, adapt it to another resolution, or integrate a new Designer project.

Before adapting the UI, make sure the Panel Driver and HoneyGUI Display Port support the target display. See the [Display Porting Guide](display-porting.md). The Designer canvas must use the same resolution as the Panel and HoneyGUI.

All paths in this document are relative to the West workspace root.

## 1. Designer Project Location

The Dashboard Designer project is a separate West project:

```text
sdk/board/evb/hmi_dashboard/src/application/designer/
```

It is managed by the manifest rather than being an ordinary directory in the hmi-dashboard repository. The default project comes from `honeygui-template-dashboard` and has an 800 × 480 resolution.

The current build consumes:

- `src/application/designer/src/`: generated UI code;
- `src/application/designer/build/app_romfs.bin`: ROMFS resources such as images and fonts;
- `GUI_INIT_APP_EXPORT(...)`: UI entry-point registration.

## 2. Choose an Integration Method

### 2.1 Modify the Default Dashboard UI

Use this workflow when retaining existing pages and interactions:

1. fork or copy the default Designer project;
2. open it with HoneyGUI Visual Designer;
3. if the display resolution changed, set the project canvas to the target resolution;
4. adjust controls, images, fonts, and interactions page by page;
5. regenerate the `src/` code and `build/app_romfs.bin`;
6. update the delivery manifest to the customer UI revision, then run `west update`;
7. perform a clean build, flash the app, and then flash the ROMFS resources.

Changing the canvas size does not produce a responsive layout automatically; inspect every page.

### 2.2 Create a New Designer Project

1. Create the project with the target resolution;
2. use a Designer/exporter version compatible with the current HoneyGUI SDK;
3. ensure the generated tree contains the required `src/` directory and registers its entry through `GUI_INIT_APP_EXPORT(...)`;
4. generate the matching `build/app_romfs.bin`;
5. place the project at the manifest path above, or point the manifest to its repository;
6. verify that the project's ROMFS base address and image size agree with the Dashboard flash map;
7. complete integration as described in [Build and Flash](#4-build-and-flash).

If the generated layout differs from `designer/src/`, update both build systems:

- GCC: Designer source/include paths in `sdk/board/evb/hmi_dashboard/gcc/CMakeLists.txt`;
- Keil MDK: `sdk/board/evb/hmi_dashboard/src/application/SConscript` and the Designer project's own `SConscript` files.

## 3. Use a Separate Customer UI Repository

Do not overwrite and commit the official Dashboard UI as the normal integration workflow. Instead, fork or copy `honeygui-template-dashboard`, maintain the UI in a customer-owned repository and branch, then point the delivery manifest's `ui` project to that repository and revision while retaining the same workspace path:

```yaml
- name: ui
  repo-path: <customer>/<customer-ui-repository>
  revision: <customer-ui-branch-or-tag>
  path: sdk/board/evb/hmi_dashboard/src/application/designer
```

This approach requires no placeholder directory in hmi-dashboard and keeps generated UI code separate from board-level porting. After `west update`, the existing GCC and Keil builds still consume UI code from `src/application/designer/src/`, and `west userdata` still consumes `src/application/designer/build/app_romfs.bin`.

A local replacement of this West project is acceptable for initial validation, but a formal delivery should pin a traceable repository commit or tag. Otherwise a later `west update` can restore the manifest-selected revision.

## 4. Build and Flash

### GCC

Make sure the selected defconfig enables the Designer UI:

```ini
CONFIG_REALTEK_BUILD_DASHBOARD_DESIGNER=y
```

Then perform a clean build:

```bash
west build -c -m src_bank0
west flash -m src_bank0
west userdata
```

Replace `src_bank0` with `lib_bank0`, `src_bank1`, or `lib_bank1` as needed, but the build and flash modes must match.

### Keil MDK

Select Dashboard Designer UI through Configuration Wizard in `menu_config.h`, then run:

```bash
scons --target=mdk5
```

Reopen the Keil project and perform a full Rebuild. After flashing the app, flash the UI ROMFS resources separately:

```bash
west userdata
```

A source-only change may not require a ROMFS update. Regenerate and flash `app_romfs.bin` whenever images, fonts, or other resources change.

## 5. Validation Checklist

- [ ] Panel, HoneyGUI, and Designer dimensions match;
- [ ] pages are not clipped, letterboxed, or misaligned;
- [ ] image and font resources were regenerated and flashed;
- [ ] touch input works correctly;
- [ ] ROMFS and other resources fit their Flash partitions;
- [ ] a clean build completes and flashes successfully.
