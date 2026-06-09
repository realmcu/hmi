# CLAW 编译说明

工程的编译入口。



## 编译


```powershell
cd zephyrproject
west build -b rtl87x3g_watch/rtl8783gbf  realtek-app\applications\claw\RustMcuClaw\mcu
```



## 产物

成功后位于：



```
zephyrproject\bin
```

### Z2PLUS

Z2PLUS 需要实现 HTTPS, WSS, ATCMD, 预生成固件位置：```RustMcuClaw\mcu\Z2plus```

## SD卡

需要拷贝```RustMcuClaw\config\SDcard\RustMcuClaw```到SD卡中，需要修改密钥（LLM,飞书）。

##  目录速览

```
claw/
├── README.md                          # 本文件
├── RustMcuClaw/mcu/                   # Zephyr 应用 (CMakeLists.txt 在此)
├── manifest/
│   └── rtl8773g-zephyr-claw.yml        # west manifest
└── west_commands_extention/
    ├── west-commands.yml              # 注册 claw-build / claw-flash / claw-clean
    ├── commands.py                    # 命令实现
    └── README.md                      # 扩展命令详细说明
```

详细命令参数和排错见 [west_commands_extention/README.md](west_commands_extention/README.md)。


