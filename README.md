# RTL8773G HMI Application

基于 Zephyr RTOS 的 RTL8773G 人机交互应用程序。

## 项目结构

本仓库包含 West manifest 配置、West 扩展命令和 HMI 应用代码：

```
hmi/
├── manifest/                           # West manifest 文件
│   └── rtl8773g-zephyr-hmi.yml        # 项目 manifest
├── west_commands_extention/           # West 扩展命令
│   ├── west-commands.yml              # 命令配置
│   ├── commands.py                    # 命令实现
│   └── README.md                      # 命令说明文档
└── README.md                           # 本文件
```

应用代码（src/、CMakeLists.txt、prj.conf 等）可以直接添加到本仓库根目录。

## 获取代码

使用 West 工具下载完整的项目：

```bash
# 初始化 West 工作区
# <your_username> 替换为你的 Gerrit 用户名，如 howie_wang
# ~/workspace/hmi-project 可替换为你想要的目录
west init -m ssh://<your_username>@cn4soc.rtkbf.com:29418/HoneyRepo/hmi --mr rtl8773g-zephyr --mf manifest/rtl8773g-zephyr-hmi.yml ~/workspace/hmi-project

# 进入工作目录
cd ~/workspace/hmi-project

# 更新所有依赖项目
west update
```

### West Init 命令参数说明

```bash
west init -m <manifest-url> --mr <branch> --mf <manifest-file> <directory>
```

**参数详解：**

- **`-m <manifest-url>`**: 指定 manifest 仓库的 URL
  - 示例: `ssh://<your_username>@cn4soc.rtkbf.com:29418/HoneyRepo/hmi`
  - 这是包含 West manifest 配置文件的 Git 仓库地址
  - West 会克隆这个仓库来获取项目的依赖配置

- **`--mr <branch>`**: 指定 manifest 仓库检出的分支（manifest revision）
  - 示例: `rtl8773g-zephyr`
  - West 克隆 manifest 仓库后会切换到此分支
  - 省略时默认使用仓库的默认分支（通常是 `master`）

- **`--mf <manifest-file>`**: 指定 manifest 仓库中的 manifest 文件路径
  - 示例: `manifest/rtl8773g-zephyr-hmi.yml`
  - 相对于 manifest 仓库根目录的路径
  - 如果 manifest 文件在根目录且命名为 `west.yml`，可以省略此参数
  - 使用此参数可以在一个仓库中管理多个不同的 manifest 配置

- **`<directory>`**: West 工作区的目标目录
  - 示例: `~/workspace/hmi-project`
  - West 将在此目录下创建 `.west/` 配置目录
  - 所有项目代码（Zephyr、模块、应用）都会被下载到这个工作区中
  - 如果省略，使用当前目录

**完整命令含义：**

在 `~/workspace/hmi-project` 目录下初始化一个 West 工作区，使用 `HoneyRepo/hmi` 仓库作为 manifest 源，并读取其中的 `manifest/rtl8773g-zephyr-hmi.yml` 文件来确定需要下载哪些依赖项目。

## 构建

```bash
# 构建应用（注意应用路径在 zephyrproject/realtek-app/applications/hmi）
cd ~/workspace/hmi-project
west build -b rtl8773g zephyrproject/realtek-app/applications/hmi

# 清理构建
west build -t clean

# 烧录到设备
west flash
```

## West 扩展命令

本项目提供了一些自定义的 West 扩展命令来简化开发流程：

### 查看项目信息
```bash
west info
```

### 清理所有构建产物
```bash
# 交互式清理
west clean-all

# 强制清理（不提示）
west clean-all -f
```

### 使用 J-Link 烧录
```bash
west flash-jlink
west flash-jlink --hex path/to/firmware.hex
```

### 构建 GUI 演示
```bash
west gui-demo
west gui-demo -b rtl8773g --pristine
```

更多详细说明请查看 [west_commands_extention/README.md](west_commands_extention/README.md)。

## 目录说明

下载完成后，工作区目录结构如下：

```
~/workspace/hmi-project/
├── .west/                              # West 配置
├── zephyr/                             # Zephyr RTOS
└── zephyrproject/                      # 项目模块
    ├── modules/
    │   ├── hal/realtek/               # Realtek HAL
    │   ├── display/                   # 显示驱动
    │   ├── wearable/                  # 可穿戴模块
    │   ├── honeygui/                  # HoneyGUI
    │   └── lvgl/                      # LVGL
    └── realtek-app/                   # Realtek 应用
        └── applications/              # 应用程序目录
            └── hmi/                   # 本仓库（HMI 应用 + manifest）
                ├── manifest/
                │   └── rtl8773g-zephyr-hmi.yml
                ├── west_commands_extention/  # West 扩展命令
                │   ├── west-commands.yml
                │   ├── commands.py
                │   └── README.md
                ├── src/               # 应用源代码（待添加）
                ├── CMakeLists.txt     # 构建文件（待添加）
                ├── prj.conf           # 配置文件（待添加）
                └── README.md          # 本文件
```

## 依赖项目

- **Zephyr RTOS**: realtek-main-v3.7
- **hal_realtek**: Realtek 硬件抽象层
- **display**: 显示驱动模块
- **wearable**: 可穿戴设备模块
- **honeygui**: HoneyGUI 图形库
- **lvgl**: LVGL v9 图形库
- **realtek-app**: Realtek 应用程序示例

## 维护者

- Owner: howie_wang
