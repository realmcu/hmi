# HMI Dashboard GCC 编译指南

HMI Dashboard 是 HoneyGUI 在 RTL8773E 平台上的演示应用，支持 GCC 编译。

## 两种构建模式

| 特性 | 源码模式 | 库模式 |
|------|----------|--------|
| 编译速度 | 较慢（首次 5-10 分钟） | 快（约 1 分钟） |
| 调式 | 可调试 GUI 源码 | 无法调试 GUI 内部 |
| Demo 选择 | 可切换 | 固定 |
| 前置条件 | 需 GUI 子仓库 | 需 `libgui.a`（已提供） |

## 编译命令

在仓库根目录 `sdk/` 下执行：

```powershell
# === 源码模式 ===
cmake -G Ninja -D kconfig_path=board/evb/hmi_app/dashboard/gcc/defconfig.RTL8773E.hmi_dashboard_src -B build
cmake --build build

# === 库模式 ===
cmake -G Ninja -D kconfig_path=board/evb/hmi_app/dashboard/gcc/defconfig.RTL8773E.hmi_dashboard_lib -B build
cmake --build build
```

> **说明**：`lib_gcc/` 下的通用 `.a` 已预编译在仓库中，无需额外处理。

## 选择 Demo（仅源码模式）

编辑 `defconfig.RTL8773E.hmi_dashboard_src`，取消注释一个 Demo 选项（只能选一个）：

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
```

## 输出文件

编译后在 `board/evb/hmi_app/dashboard/bin/<config_name>/`：

```
honeygui_bank0.elf       ELF 文件
honeygui_bank0.bin       烧录用 BIN
honeygui_bank0_MP.bin    含签名的 BIN
honeygui_bank0.map       内存映射
```

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

### 编译失败

清理后重试：
```powershell
Remove-Item -Recurse -Force build
cmake -G Ninja -D kconfig_path=... -B build
cmake --build build
```
