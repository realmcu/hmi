# Ameba HoneyGUI Application

基于 Ameba RTL8721F 的 HoneyGUI 人机交互应用程序。

## 项目结构

```
hmi/
├── acc/                                # 硬件加速模块
│   ├── CMakeLists.txt
│   ├── acc_jpeg.c                      # JPEG 硬件加速
│   ├── acc_ppe.c                       # PPE 硬件加速
│   ├── rtl_ppe.c                       # RTL PPE 驱动
│   ├── rtl_ppe.h
│   └── rtl_ppe_reg.h                   # RTL PPE 寄存器定义
├── port/                               # GUI 移植层
│   ├── CMakeLists.txt
│   ├── gui_port.h
│   ├── gui_port_acc.c                  # 硬件加速移植
│   ├── gui_port_dc.c                   # 显示控制器移植
│   ├── gui_port_indev.c                # 输入设备移植
│   ├── gui_port_init.c                 # 初始化 & 组件注册
│   ├── gui_port_init.h
│   ├── gui_port_os.c                   # 操作系统移植
│   └── readme.md
├── app/                                # 应用程序
│   ├── CMakeLists.txt
│   ├── Kconfig
│   └── dashboard/                      # Dashboard 应用
│       ├── CMakeLists.txt
│       ├── ble/                        # BLE 通信
│       │   ├── dashboard_ble.c
│       │   ├── dashboard_ble.h
│       │   └── dashboard_hid_service/
│       │       ├── dashboard_hids_cc.c
│       │       └── dashboard_hids_cc.h
│       ├── bt_ext/                     # BT EXT 通信
│       │   ├── bt_ext_hids_cc.c
│       │   ├── bt_ext_hids_cc.h
│       │   ├── dashboard_bt_ext.c
│       │   └── dashboard_bt_ext.h
│       ├── io_peripheral/              # 按键输入
│       │   ├── dashboard_key.c
│       │   └── dashboard_key.h
│       ├── src/
│       │   └── dashboard_main.c        # 主入口
│       ├── ui_design/                  # UI 设计（由 HoneyGUI Designer 生成）
│       │   ├── callbacks/
│       │   │   ├── DashboardMain_callbacks.c
│       │   │   └── DashboardMain_callbacks.h
│       │   ├── ui/
│       │   │   ├── DashboardMain_ui.c
│       │   │   └── DashboardMain_ui.h
│       │   ├── user/
│       │   │   ├── DashboardMain_user.c
│       │   │   └── DashboardMain_user.h
│       │   ├── app_romfs.bin
│       │   └── dashboardEntry.c
│       └── wifi/                       # Wi-Fi & OTA
│           ├── dashboard_ota_http.c
│           ├── dashboard_ota_http.h
│           ├── dashboard_wifi.c
│           └── dashboard_wifi.h
├── demos/                              # HoneyGUI 入口 & 版本信息
│   ├── CMakeLists.txt
│   ├── honeygui_demos.c
│   └── honeygui_demos.h
├── CMakeLists.txt                      # 顶层构建文件
├── Kconfig                             # HoneyGUI Kconfig 配置
└── README.md                           # 本文件
```

### 工作区目录结构

```
~/workspace/hmi-project/
├── .west/                              # West 配置
└── ameba-rtos/                         # Ameba RTOS SDK
    ├── component/
    │   ├── audio/                      # 音频模块 (git submodule)
    │   ├── ui/                         # UI 模块 (git submodule)
    │   │   └── HoneyGUI/
    │   │       ├── Kconfig             # HoneyGUI Kconfig (由 ameba-ui 提供)
    │   │       ├── app/                # 本仓库 (manifest 仓库)
    │   │       │   ├── Kconfig
    │   │       │   ├── CMakeLists.txt
    │   │       │   ├── port/
    │   │       │   ├── app/
    │   │       │   ├── demos/
    │   │       │   └── acc/
    │   │       └── honeygui/           # HoneyGUI 图形库
    │   ├── aivoice/                    # AI 语音模块 (git submodule)
    │   └── tflite_micro/              # TensorFlow Lite Micro (git submodule)
    └── ...
```
## 构建

### 方式一：使用 VS Code 扩展插件（推荐）

安装 Ameba VS Code 扩展插件，可一键完成 SDK 环境配置、项目编译和固件烧录：

1. 参考 [VS Code 使用指南](https://aiot.realmcu.com/cn/latest/rst_tools/vscode/index.html) 安装插件
2. 用 VS Code 打开工作区目录 `~/workspace/ameba-honeygui`
3. 使用插件提供的图形化界面完成配置和编译

### 方式二：手动配置编译环境

参考 [FreeRTOS SDK 使用指南](https://aiot.realmcu.com/cn/latest/rst_rtos/rst_sdk/index.html)，逐步进行以下操作：

```bash
cd ~/workspace/ameba-honeygui/ameba-rtos

# 1. SDK 环境配置
#    Linux:   source env.sh
#    Windows: env.bat

# 2. 选择目标芯片
#    HoneyGUI 目前支持 AmebaGreen2 系列 (RTL8721F)
python ameba.py soc rtl8721f

# 3. 工程配置
#    启用：Graphics Libraries → Use HoneyGUI
#    进入 HoneyGUI Configuration 进行详细配置
python ameba.py menuconfig

# 4. 工程编译
python ameba.py build

# 5. 固件烧录（替换 <PORT>、<BAUDRATE> 为实际值）
python ameba.py flash -p <PORT> -b <BAUDRATE> -i <BIN_FILE> <START_ADDR> <END_ADDR>

# 6. 串口监控（可选）
python ameba.py monitor -p <PORT> -b 1500000
```

## 配置说明

HoneyGUI 相关 Kconfig 配置项：

- `GRAPHIC_UI` — 启用图形 UI 模块（位于 `ameba-rtos/component/ui/Kconfig`）
- `USE_HONEYGUI` — 选择 HoneyGUI 作为图形库
- `HONEYGUI_ENABLE` — 启用 HoneyGUI（由 `USE_HONEYGUI` 自动选中）
- `REALTEK_BUILD_HONEYGUI_APP` — 构建 HoneyGUI 应用程序
- `HONEYGUI_APP_DASHBOARD` — 构建 Dashboard 子应用

