# HMI 编译说明

RTL8773G HMI / RustMcuClaw MCU 工程的编译入口。

##  首次准备 (只做一次)

确认顶层 west workspace 已经就绪：

```powershell
cd C:\Users\triton_yu\Documents\hmi\zephyrproject
Get-Content .west\config
```

`.west\config` 文件应包含以下内容：

```ini
[manifest]
path = realtek-app
file = applications/hmi/manifest/rtl8773g-zephyr-hmi.yml

[zephyr]
base = zephyr
```

这段配置会告诉 `west` 使用 `realtek-app` 作为 manifest 项目，并告诉 `west` 使用
`applications/hmi/manifest/rtl8773g-zephyr-hmi.yml` 作为 manifest 文件。同时，这段配置还会告诉构建系统使用顶层
`zephyr\` 目录作为 Zephyr 基础目录。

如果顶层目录中不存在 `.west\` 目录，请你手动创建 `.west\config` 文件，并把上述内容写入该文件。完成这一步后，请你执行以下命令：

```powershell
west update            # 这条命令会拉取或更新 manifest 中声明的 projects
west zephyr-export     # 这条命令会把当前 zephyr 注册给 CMake
```

## 编译

```powershell
cd C:\Users\triton_yu\Documents\hmi\zephyrproject
west hmi-build            # 默认 rtl87x3g_watch/rtl8783gbf
west hmi-build -p always  # pristine 重新生成
```

等价的原始命令：

```powershell
west build -b rtl87x3g_watch/rtl8783gbf ` realtek-app\applications\hmi\RustMcuClaw\mcu
```



## 产物

成功后位于：

```
RustMcuClaw\mcu\bin\app.bin
RustMcuClaw\mcu\bin\app.elf
RustMcuClaw\mcu\bin\app_MP-*.bin     # 已签名 + MP header
RustMcuClaw\mcu\build\zephyr\zephyr.elf
```

或者 

```
zephyrproject\bin
```

### Z2PLUS

Z2PLUS 需要实现 HTTPS, WSS, ATCMD, 预生成固件位置：```RustMcuClaw\mcu\Z2plus```

## SD卡

需要拷贝```RustMcuClaw\config\SDcard\RustMcuClaw```到SD卡中，需要修改密钥（LLM,飞书）。

##  目录速览

```
hmi/
├── README.md                          # 本文件
├── RustMcuClaw/mcu/                   # Zephyr 应用 (CMakeLists.txt 在此)
├── manifest/
│   └── rtl8773g-zephyr-hmi.yml        # west manifest
└── west_commands_extention/
    ├── west-commands.yml              # 注册 hmi-build / hmi-flash / hmi-clean
    ├── commands.py                    # 命令实现
    └── README.md                      # 扩展命令详细说明
```

详细命令参数和排错见 [west_commands_extention/README.md](west_commands_extention/README.md)。


