# HMI Dashboard GCC 编译指南

HMI Dashboard 是 HoneyGUI 在 RTL8773E 平台上的演示应用，支持 GCC 编译。

## 构建模式矩阵

| 特性 | 源码模式（src） | 库模式（lib） |
|------|----------------|--------------|
| 编译速度 | 较慢（首次 5-10 分钟） | 快（约 1 分钟） |
| 调试 | 可调试 GUI 源码 | 无法调试 GUI 内部 |
| Demo 选择 | 可切换 | 固定 |
| 前置条件 | 需 GUI 子仓库 | 需 `libgui.a`（已提供） |

## 编译命令

在仓库根目录 `sdk/` 下执行：

```powershell
# 默认：源码模式，A 槽
cmake -G Ninja -D kconfig_path=board/evb/hmi_dashboard/gcc/defconfig.RTL8773E.hmi_dashboard_src_bank0 -DIS_CHECK_FLOW=off -Dcompile_lib_only=OFF -B build
cmake --build build
```

其他 mode 只需将末尾替换：`src_bank0` → `src_bank1` / `lib_bank0` / `lib_bank1`。

> **推荐**：日常迭代使用 `lib_bank0`；需要调试 GUI 源码时切换到 `src_bank0`。

## 选择 Demo（仅源码模式）

编辑 `defconfig.RTL8773E.hmi_dashboard_src_bank0`，取消注释一个 Demo 选项（只能选一个）：

> `src_bank1` 的 Demo 选项相同，两者只相差一行 `CONFIG_REALTEK_COMPILE_BANK1=y`。

```ini
# 2D 图形
CONFIG_REALTEK_BUILD_EXAMPLE_IMAGE_WIDGET=y
# CONFIG_REALTEK_BUILD_EXAMPLE_SVG_WIDGET=y
# CONFIG_REALTEK_BUILD_EXAMPLE_GIF_WIDGET=y
# CONFIG_REALTEK_BUILD_EXAMPLE_TEXT_WIDGET=y

# 3D 图形
# CONFIG_REALTEK_BUILD_REAL_DOG_3D=y
# CONFIG_REALTEK_BUILD_REAL_EARTH_3D=y
# CONFIG_REALTEK_BUILD_REAL_DONUT_3D=y

# 屏幕适配
# CONFIG_REALTEK_BUILD_GUI_800_480_DEMO=y
# CONFIG_REALTEK_BUILD_GUI_466_466_DEMO=y
# CONFIG_REALTEK_BUILD_GUI_240_240_DEMO=y

# HML Designer
# CONFIG_REALTEK_BUILD_HML_DESIGNER=y

# ... 更多选项见 Kconfig.gui
```

## 输出文件

编译后产物隔离到 `board/evb/hmi_dashboard/bin/RTL8773E.hmi_dashboard_<mode>/`：

```
# 以 src_bank0 为例：bin/RTL8773E.hmi_dashboard_src_bank0/
honeygui_src.elf              ELF 文件
dashboard_bank0_MP-*.bin      含签名的烧录 BIN

# bank1 mode 产物（bin/RTL8773E.hmi_dashboard_src_bank1/ 或 lib_bank1/）
dashboard_bank1_MP-*.bin      含签名的烧录 BIN
```

## 功能配置

`defconfig.RTL8773E.hmi_dashboard_src_bank0` 中可用的 GUI 功能开关：

```ini
# 3D 图形
CONFIG_REALTEK_BUILD_LITE3D=y

# 物理引擎
CONFIG_REALTEK_BUILD_GUI_BOX2D=n

# 粒子系统
CONFIG_REALTEK_BUILD_PARTICLE_SYSTEM=n

# HML Designer 支持
CONFIG_REALTEK_BUILD_XML_LOADER=n

# 调试工具
CONFIG_REALTEK_BUILD_LETTER_SHELL=y
CONFIG_REALTEK_BUILD_MONKEY_TEST=n
```

## 设备配置

```ini
# LCD 屏幕
CONFIG_REALTEK_LCD_ST7265_800480_RGB=y

# ROMFS 支持
CONFIG_REALTEK_ROMFS=y
```

## 与 MDK 的对比

| 维度 | MDK | GCC |
|------|-----|-----|
| 配置工具 | menu_config.h | defconfig + Kconfig |
| 构建工具 | scons | cmake |
| 源码/库模式 | menuconfig 选项 | defconfig 选择 |
| Demo 选择 | menu_config.h 宏 | Kconfig choice |

## 常见问题

### 提示找不到编译器

确保 Arm GNU Toolchain 在 PATH 中：

```powershell
arm-none-eabi-gcc --version
# 应输出: Arm GNU Toolchain 12.3.Rel1 ... 12.3.1
```

推荐版本：`C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\12.3 rel1\bin`

### 源码模式缺少 GUI 源文件

检查 GUI 子仓库是否已拉取：
```powershell
ls src/sample/gui/
```

### 库模式找不到 libgui.a

检查预编译库是否存在：
```powershell
ls board/evb/hmi_dashboard/src/gui_lib/gcc/libgui.a
```

### 编译失败

1. 清理 build 目录后重试
2. 检查 defconfig 语法
3. 确认工具链已正确安装

```powershell
Remove-Item -Recurse -Force build
cmake -G Ninja -D kconfig_path=... -B build
cmake --build build
```
