# CLAW West 扩展命令

本目录提供 `realtek-app/applications/claw/RustMcuClaw/mcu` 工程专用的 West
扩展命令，省去每次手动输入 `-b` 和应用路径的麻烦。

文件结构：

```
applications/claw/
├── manifest/
│   └── rtl8773g-zephyr-claw.yml        # 项目 manifest，引用本目录的 west-commands.yml
└── west_commands_extention/
    ├── west-commands.yml              # 注册三个 claw-* 命令
    ├── commands.py                    # 命令实现 (HmiBuild / HmiFlash / HmiClean)
    └── README.md                      # 本文档
```

等价的原始命令（任何时候仍然可用）：

```powershell
west build -b rtl87x3g_watch/rtl8783gbf `
    realtek-app\applications\claw\RustMcuClaw\mcu
west flash -d realtek-app\applications\claw\RustMcuClaw\mcu\build
```

## 4. 默认值

`commands.py` 顶部集中了三个默认值，按需修改：

| 变量 | 默认值 |
| --- | --- |
| `_APP_DIR`           | `applications/claw/RustMcuClaw/mcu` |
| `_DEFAULT_BOARD`     | `rtl87x3g_watch/rtl8783gbf` |
| `_DEFAULT_BUILD_DIR` | `<_APP_DIR>/build` |

## 5. 常见问题

**`FATAL ERROR: no west workspace found ...`**
当前目录不在已初始化的 west workspace 内，或者 `.west/config` 被删了。
按第 1 节重新 `west init -l` 即可。

**`west: error: argument <command>: invalid choice: 'claw-build'`**
说明 manifest 没有指向本目录的 `west-commands.yml`，常见原因：

1. `west config manifest.path` 不是 `realtek-app/applications/claw/manifest`。
2. 手动改过 manifest 后没有再次 `west update`。

**Rust 目标找不到**
首次编译前需要安装 thumbv8 目标：

```powershell
rustup target add thumbv8m.main-none-eabihf
```
