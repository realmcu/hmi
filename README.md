# CLAW Build Instructions

The build entry point for the project.



## Build


```powershell
cd zephyrproject
west build -b rtl87x3g_watch/rtl8783gbf  realtek-app\applications\claw\RustMcuClaw\mcu
```



## Output

On success, located at:



```
zephyrproject\bin
```

### Z2PLUS

Z2PLUS needs HTTPS, WSS, ATCMD implemented; prebuilt firmware location: ```RustMcuClaw\mcu\Z2plus```

## SD Card

Copy ```RustMcuClaw\config\SDcard\RustMcuClaw``` to the SD card, and update the keys (LLM, Feishu).

##  Directory Overview

```
claw/
├── README.md                          # This file
├── RustMcuClaw/mcu/                   # Zephyr application (CMakeLists.txt is here)
├── manifest/
│   └── rtl8773g-zephyr-claw.yml        # west manifest
└── west_commands_extention/
    ├── west-commands.yml              # Registers claw-build / claw-flash / claw-clean
    ├── commands.py                    # Command implementations
    └── README.md                      # Detailed extension command docs
```

For detailed command parameters and troubleshooting, see [west_commands_extention/README.md](west_commands_extention/README.md).


