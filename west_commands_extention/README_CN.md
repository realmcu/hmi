# West 扩展命令

RTL8773E Dashboard 项目的 West 自定义命令（仅面向 GCC/CMake 构建，MDK 工程请直接在 Keil 中操作）。

## 可用命令

### `west info`

显示工作区路径、构建状态和输出 ELF 文件。

```bash
west info
```

### `west build`

封装 cmake configure + ninja build，省去每次手打长命令。

```bash
# 默认：源码 GUI + bank0（等价 -m src_bank0）
west build

# 完整 4 种 mode：<gui>_<bank>
west build -m src_bank0   # 源码 GUI，A 槽（默认）
west build -m src_bank1   # 源码 GUI，B 槽
west build -m lib_bank0   # 预编译 libgui.a，A 槽（迭代更快）
west build -m lib_bank1   # 预编译 libgui.a，B 槽

# 向后兼容别名：-m src → src_bank0、-m lib → lib_bank0
west build -m src
west build -m lib

# 先清理再编译
west build -c

# 并行编译（8 线程）
west build -j 8

# 仅 cmake configure，跳过 build（用于调试 cmake 配置）
west build --configure-only
```

等价的手动命令：

```powershell
# 在 sdk/ 目录下，以 src_bank0 为例
cmake -G Ninja -D kconfig_path=board/evb/hmi_dashboard/gcc/defconfig.RTL8773E.hmi_dashboard_src_bank0 -DIS_CHECK_FLOW=OFF -Dcompile_lib_only=OFF -B build
cmake --build build
```

### `west clean`

删除 cmake build 目录。

> 每个 mode 使用独立的 build 子目录（`build/<mode>/`，如 `build/lib_bank1/`），
> 因此切换 mode 无需重新全量编译，各自保留增量缓存，也不会互相串味。

```bash
# 删除所有 mode 的 build 目录（整个 build/）
west clean

# 只删除某个 mode 的 build 目录
west clean -m lib_bank1

# 同时删除 board/evb/hmi_dashboard/bin/ 输出目录
west clean --all
```

### `west flash`

调用 `gcc/download.bat`，自动定位 MP binary 并通过串口烧录到设备。

```bash
# 使用 download.bat 默认串口（COM3），默认 mode = src_bank0
west flash

# 指定串口
west flash -p COM5

# 烧录 bank1 镜像（必须与之前 west build -m 的 mode 对应）
west flash -m src_bank1
west flash -m lib_bank1 -p COM5

# 同时烧录 userdata 分区
west flash -p COM3 --userdata path/to/userdata.bin --userdata-addr 0x00A00000
```

> `-m` 决定从哪个 `bin/RTL8773E.hmi_dashboard_<mode>/` 目录捞取 `dashboard_<bank>_MP-*.bin`。
> 依赖 `download/mpcli/mpcli.exe`，烧录完成后会有 `[DONE]` 或 `[FAILED]` 提示。

### `west size`

调用 `arm-none-eabi-size` 显示各 section 内存占用，以及 MP binary 大小。

```bash
west size
```

示例输出：

```text
ELF: .../gcc/bin/RTL8773E.hmi_dashboard_src_bank0/honeygui_src.elf

section              size      addr
.text              123456  0x00100000
.data                1234  0x00200000
.bss                 5678  0x00201000
...

MP binary : 126,976 bytes  (124.0 KB)
```

### `west guilib`

从源码重新编译 HoneyGUI 的两套静态库——armclang（MDK/Keil）和
arm-none-eabi-gcc（GCC lib 模式）——并同步到 `src/gui_lib/`。

```bash
west guilib
```

依次执行两个编译脚本：

- `lib/armclang/bulidRTL8773E.bat`（Keil armclang 工具链，
  默认路径 `C:/Keil_v5/ARM/ArmCompilerforEmbedded6.22`）
- `lib/arm-none-eabi-gcc/bulidRTL8773E.bat`（优先用 PATH 里的
  `arm-none-eabi-gcc`，否则 fallback 到
  `C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/13.2 Rel1/bin`）

再把各自编译产物拷贝到 `board/evb/hmi_dashboard/src/gui_lib/`：

- `lib/armclang/install/lib/gui.lib` → `src/gui_lib/armclang/gui.lib`
- `lib/arm-none-eabi-gcc/install/lib/libgui.a` → `src/gui_lib/gcc/libgui.a`
- `install/include/*`（两边内容一致） → `src/gui_lib/include/`

> 拷贝前会依次清空 `armclang/`、`gcc/`、`include/` 三个目标目录，
> 避免上游已删除的文件残留误导。

### `west sync`

**替代 `west update` 的推荐命令**，按顺序执行三步：

1. **强制更新 manifest 仓库**（`.manifest/`）：`git fetch origin` + `git reset --hard origin/<branch>`
2. **`west update`**：按最新 manifest YAML 同步所有 West project
3. **submodule 更新**：对所有含 `.gitmodules` 的 project 执行 `git submodule update --init --recursive`

```bash
west sync

# 可以透传任何 west update 的原生参数
west sync --narrow
west sync -o=--depth=1
```

> manifest 仓库处于 detached HEAD 时，步骤 1 会跳过 reset 并打印警告，不阻断后续流程。

## MDK 工程

MDK 工程使用 `board/evb/hmi_dashboard/mdk/` 下的 Keil 工程文件，与 west 无关，直接在 Keil IDE 中编译和下载即可。

## 添加新命令

1. 在 `west-commands.yml` 中注册：

```yaml
- name: my-command
  class: MyCommand
  help: 命令描述
```

2. 在 `commands.py` 中实现：

```python
class MyCommand(WestCommand):
    def __init__(self):
        super().__init__('my-command', 'short help', 'description')

    def do_add_parser(self, parser_adder, **kwargs):
        return parser_adder.add_parser(self.name, help=self.help)

    def do_run(self, args, unknown_args):
        topdir = self.manifest.topdir
        log.inf(f'workspace: {topdir}')
```

## 参考

- [West Extension Commands](https://docs.zephyrproject.org/latest/develop/west/extensions.html)
