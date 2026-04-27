# RTL8773G HMI Application

基于 Zephyr RTOS 的 RTL8773G 人机交互应用程序。

## 项目结构

```
hmi/
├── manifest/                           # West manifest 文件
│   └── rtl8773g-zephyr-hmi.yml        # 项目 manifest
├── src/                                # 应用源代码
│   └── main.c
├── boards/                             # 板级配置文件
├── dts/                                # 设备树覆盖文件
├── CMakeLists.txt                      # CMake 构建文件
├── prj.conf                            # 项目配置
└── README.md                           # 本文件
```

## 获取代码

使用 West 工具下载完整的项目：

```bash
# 初始化 West 工作区
west init -m ssh://howie_wang@cn4soc.rtkbf.com:29418/HoneyRepo/hmi \
          --mf manifest/rtl8773g-zephyr-hmi.yml \
          ~/workspace/hmi-project

# 进入工作目录
cd ~/workspace/hmi-project

# 更新所有依赖项目
west update

# 导出 Zephyr CMake 包
west zephyr-export
```

## 构建

```bash
# 构建应用
cd ~/workspace/hmi-project
west build -b rtl8773g hmi

# 清理构建
west build -t clean

# 烧录到设备
west flash
```

## 目录说明

下载完成后，工作区目录结构如下：

```
~/workspace/hmi-project/
├── .west/                              # West 配置
├── hmi/                                # 本仓库（应用代码 + manifest）
│   ├── manifest/
│   ├── src/
│   ├── CMakeLists.txt
│   └── prj.conf
├── zephyr/                             # Zephyr RTOS
└── zephyrproject/                      # 其他模块
    ├── modules/
    │   ├── hal/realtek/               # Realtek HAL
    │   ├── display/                   # 显示驱动
    │   ├── wearable/                  # 可穿戴模块
    │   ├── honeygui/                  # HoneyGUI
    │   └── lvgl/                      # LVGL
    └── realtek-app/                   # Realtek 应用示例
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
