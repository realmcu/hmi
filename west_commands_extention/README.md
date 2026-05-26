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

## 1. 初始化 West Workspace

如果 `C:\Users\triton_yu\Documents\claw\zephyrproject\.west` 还不存在，先用
本工程的 manifest 把 workspace 初始化为本地 (local) 模式：

```powershell
cd zephyrproject
west init -l realtek-app\applications\claw\manifest
west update                                   # 拉/更新所有 projects
west zephyr-export                            # 让 CMake 能找到 Zephyr
```

`west init -l` 会读取 `manifest/rtl8773g-zephyr-claw.yml`，把它登记成当前
workspace 的 manifest，并写入 `.west/config`。manifest 中的
`self.west-commands: ../west_commands_extention/west-commands.yml` 会让
west 自动加载本目录下的扩展命令。

> 如果你的 `zephyrproject/` 目录已经是另一个 west workspace 的一部分，
> 不需要重新 `west init`，只需切换 manifest 即可：
>
> ```powershell
> west config manifest.path realtek-app/applications/claw/manifest
> west config manifest.file rtl8773g-zephyr-claw.yml
> ```

## 2. 验证扩展命令是否加载

```powershell
west help | Select-String claw-
```

应当能看到：

```
  claw-build:  编译 realtek-app/applications/claw/RustMcuClaw/mcu ...
  claw-flash:  烧录上一次 claw-build 产生的镜像
  claw-clean:  删除 claw 工程的 build 目录
```

## 3. 日常使用

```powershell
# 默认板子 rtl87x3g_watch/rtl8783gbf
west claw-build

# 切换到另一个板子
west claw-build -b rtl87x3g_watch/rtl8773gtp

# pristine 重新编译
west claw-build -p always

# 把额外参数透传给底层 west build / CMake
west claw-build -- -DEXTRA_CONF_FILE=prj_debug.conf



# 清理 build 目录
west claw-clean
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
