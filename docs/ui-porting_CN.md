# UI 工程移植指南

Dashboard SDK 默认提供一套 Dashboard Designer UI。如果需要修改默认 UI、适配其他分辨率或使用全新的 Designer 工程，可以按照本文接入。

开始 UI 适配前，应先确保 Panel Driver 和 HoneyGUI Display Port 已适配目标屏幕，参见[显示屏移植指南](display-porting_CN.md)。Designer 画布尺寸应与 Panel 和 HoneyGUI 的分辨率保持一致。

本文路径均相对于 West workspace 根目录。

## 1. Designer 工程位置

Dashboard Designer 工程是独立的 West project：

```text
sdk/board/evb/hmi_dashboard/src/application/designer/
```

它由 manifest 管理，并非 hmi-dashboard 仓库中的普通子目录。默认工程来自 `honeygui-template-dashboard`，分辨率为 800 × 480。

现有构建流程使用以下内容：

- `src/application/designer/src/`：Designer 生成的 UI 代码；
- `src/application/designer/build/app_romfs.bin`：图片、字体等 ROMFS 资源；
- `GUI_INIT_APP_EXPORT(...)`：注册 UI 入口。

## 2. 选择接入方式

### 2.1 修改默认 Dashboard UI

适合希望保留现有页面和交互的项目：

1. fork 或复制默认 Designer 工程；
2. 使用 HoneyGUI Visual Designer 打开该工程；
3. 如果屏幕分辨率发生变化，将项目画布改为目标分辨率；
4. 逐页调整控件位置、尺寸、图片、字体和交互；
5. 重新生成 `src/` 代码以及 `build/app_romfs.bin`；
6. 将客户 UI 仓库的 revision 写入交付 manifest，并执行 `west update`；
7. clean build、烧录 app，再烧录 ROMFS 资源。

改变画布尺寸不会自动完成响应式布局，必须逐页检查。

### 2.2 新建 Designer 工程

1. 使用目标分辨率新建工程；
2. 使用与当前 HoneyGUI SDK 兼容的 Designer/exporter 版本；
3. 确保生成目录中包含构建所需的 `src/`，入口通过 `GUI_INIT_APP_EXPORT(...)` 注册；
4. 生成与该 UI 匹配的 `build/app_romfs.bin`；
5. 将工程置于上述 manifest 路径，或将 manifest 改为客户 UI 仓库；
6. 确认工程的 ROMFS base address 和产物大小符合 Dashboard flash map；
7. 按[构建和烧录](#4-构建和烧录)完成集成。

如果新工程的生成目录结构与 `designer/src/` 不同，需要同步修改：

- GCC：`sdk/board/evb/hmi_dashboard/gcc/CMakeLists.txt` 中的 Designer source/include 路径；
- Keil MDK：`sdk/board/evb/hmi_dashboard/src/application/SConscript` 及 Designer 工程内的 `SConscript`。

## 3. 使用独立的客户 UI 仓库

不建议直接覆盖并提交官方 Dashboard UI。推荐 fork 或复制 `honeygui-template-dashboard`，在客户自己的仓库和分支中维护 UI，然后让交付 manifest 的 `ui` project 指向该仓库和 revision，同时保留同一个 workspace 路径：

```yaml
- name: ui
  repo-path: <customer>/<customer-ui-repository>
  revision: <customer-ui-branch-or-tag>
  path: sdk/board/evb/hmi_dashboard/src/application/designer
```

这种方式不需要在 hmi-dashboard 中新增占位目录，也不会把生成代码与板级移植混入同一仓库。执行 `west update` 后，现有 GCC 和 Keil 构建仍从 `src/application/designer/src/` 读取 UI 代码，`west userdata` 仍从 `src/application/designer/build/app_romfs.bin` 读取资源。

如果暂时不能维护独立远端，也可以在本地替换该 West project 的内容进行验证。但正式交付前应固定到可追溯的仓库 commit 或 tag，避免后续 `west update` 将本地内容切回 manifest 指定版本。

## 4. 构建和烧录

### GCC

确保所用 defconfig 启用了 Designer UI：

```ini
CONFIG_REALTEK_BUILD_DASHBOARD_DESIGNER=y
```

然后执行 clean build：

```bash
west build -c -m src_bank0
west flash -m src_bank0
west userdata
```

也可以将 `src_bank0` 替换为实际使用的 `lib_bank0`、`src_bank1` 或 `lib_bank1`，但 build 与 flash 的 mode 必须一致。

### Keil MDK

在 `menu_config.h` 的 Configuration Wizard 中选择 Dashboard Designer UI，运行：

```bash
scons --target=mdk5
```

重新打开 Keil 工程并执行完整 Rebuild。烧录 app 后，还要烧录 UI 的 ROMFS 资源：

```bash
west userdata
```

只修改 C 源码时不一定需要更新 ROMFS；图片、字体或其他资源变化后必须重新生成并烧录 `app_romfs.bin`。

## 5. 验证清单

- [ ] Panel、HoneyGUI 和 Designer 三处宽高一致；
- [ ] 页面无裁切、留白和控件错位；
- [ ] 图片和字体资源已重新生成并烧录；
- [ ] 触摸输入正常；
- [ ] ROMFS 和其他资源未超出 Flash 分区；
- [ ] clean build 后可正常构建和烧录。
