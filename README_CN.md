# RTL8773E HMI Application

RTL8773E HMI 应用程序，使用 West 进行多仓库管理。

## 项目结构

West 工作区由两个仓库组成，hmi 作为 manifest repo 嵌套在 honeycomb SDK 内部：

```
workspace/                                          # West 工作区根（.west/ 在这里）
├── .west/                                          # West 配置
└── honeycomb/                                      # [project] Release SDK 大仓库
    └── sdk/
        ├── bin/
        ├── board/
        │   └── evb/
        │       └── hmi/                            # [self] 本仓库 (manifest repo)
        │           ├── manifest/
        │           │   └── rtl8773e-hmi.yml
        │           ├── west_commands_extention/
        │           │   ├── west-commands.yml
        │           │   └── commands.py
        │           └── README.md
        ├── config/
        ├── doc/
        ├── src/
        └── ...
```

## 获取代码

```bash
# 初始化 West 工作区
# <your_username> 替换为你的 Gerrit 用户名，如 howie_wang
# ~/workspace/hmi-project 可替换为你想要的目录
# --mr rtl8773e 指定使用 rtl8773e 分支
west init -m ssh://<your_username>@cn4soc.rtkbf.com:29418/HoneyRepo/hmi --mf manifest/rtl8773e-hmi.yml --mr rtl8773e ~/workspace/hmi-project

cd ~/workspace/hmi-project

west update
```

### 利用已有仓库构建 West 工作区

如果你本地已有 honeycomb SDK 仓库（例如之前克隆过 release-crb-3.14.0），可以直接复用，无需重新下载：

```bash
# 1. 创建工作区目录
mkdir ~/workspace/hmi-project
cd ~/workspace/hmi-project

# 2. 将已有的 honeycomb 仓库拷贝（或移动）到工作区下
# 确保 .git 目录位于 honeycomb/.git
cp -r /path/to/your/existing/honeycomb ~/workspace/hmi-project/honeycomb

# 3. Clone hmi（manifest repo）到 honeycomb 内的指定位置
git clone ssh://<your_username>@cn4soc.rtkbf.com:29418/HoneyRepo/hmi honeycomb/sdk/board/evb/hmi

# 4. 初始化 West
# 注意：必须使用手动创建配置文件的方式，不能使用 west init -l（会导致重复 clone）
mkdir .west
cat > .west/config << EOF
[manifest]
path = honeycomb/sdk/board/evb/hmi
file = manifest/rtl8773e-hmi.yml
EOF

# 5. 更新工作区
west update
```

> **说明：** 步骤 2 复用已有的 honeycomb SDK，避免重新 clone 大仓库。West 检测到 `honeycomb/` 下已有 `.git` 目录时会直接复用，只做 fetch 和 checkout。步骤 3 clone hmi 后需要手动切换到 rtl8773e 分支。如果完全无法访问远程仓库，可以使用 `west update --fetch=never` 跳过 fetch 步骤。

## 依赖仓库

| 仓库 | 说明 |
|------|------|
| release-crb-3.14.0 | RTL8773E Release SDK |

## 编译方式

### MDK 编译

```bash
cd board/evb/hmi_app/dashboard

# 修改 menu_config.h 选择配置
# - GUI 构建模式：源码或预编译库
# - Demo 选择
# - 功能配置

# 使用 scons 生成 MDK 工程
scons

# 生成的工程文件位于 mdk/ 目录
# 使用 Keil MDK 打开 mdk/project.uvprojx 进行编译
```

### GCC 编译

HMI Dashboard 支持 GCC 编译，提供两种模式：

#### 1. 源码编译模式

使用 GUI 子仓库源码编译，支持选择不同 Demo：

```bash
cd honeycomb/sdk

# 创建构建目录
mkdir -p build/dashboard_src
cd build/dashboard_src

# 配置（使用源码 defconfig）
cmake ../.. -Dkconfig_path=board/evb/hmi_app/dashboard/gcc/defconfig.RTL8773E.16M_bank0_src

# 编译
cmake --build . --target honeygui
```

#### 2. 预编译库模式

链接预编译的 `libgui.a`：

```bash
cd honeycomb/sdk

mkdir -p build/dashboard_lib
cd build/dashboard_lib

cmake ../.. -Dkconfig_path=board/evb/hmi_app/dashboard/gcc/defconfig.RTL8773E.16M_bank0_lib

cmake --build . --target honeygui
```

详细说明请参考 [gcc/README_CN.md](dashboard/gcc/README_CN.md)。

## 配置对照

| MDK (menu_config.h) | GCC (defconfig) |
|---------------------|-----------------|
| `CONFIG_REALTEK_HONEYGUI_BUILD_MODE = 1` | `CONFIG_REALTEK_HONEYGUI_BUILD_SRC=y` |
| `CONFIG_REALTEK_HONEYGUI_BUILD_MODE = 0` | `CONFIG_REALTEK_HONEYGUI_BUILD_LIB=y` |
| `CONFIG_REALTEK_HONEYGUI_DEMO_SELECT` | Kconfig choice 菜单选择 |

## 输出文件

编译输出位于 `board/evb/hmi_app/dashboard/bin/<config_name>/`：

- `honeygui_bank0.elf` - ELF 可执行文件
- `honeygui_bank0.hex` - HEX 固件
- `honeygui_bank0_MP.bin` - 带签名的 BIN 固件
