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
# 默认：source 模式，bank0
west build

# library 模式（使用预编译 libgui.a，编译更快）
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
# 在 honeycomb/sdk/ 目录下
cmake -G Ninja -Dkconfig_path=board/evb/hmi_dashboard/gcc/defconfig.RTL8773E.hmi_dashboard_src -B build
cmake --build build
```

### `west clean`

删除 cmake build 目录。

```bash
# 只删除 build/
west clean

# 同时删除 board/evb/hmi_dashboard/bin/ 输出目录
west clean --all
```

### `west flash`

调用 `gcc/download.bat`，自动定位 MP binary 并通过串口烧录到设备。

```bash
# 使用 download.bat 默认串口（COM3）
west flash

# 指定串口
west flash -p COM5

# 同时烧录 userdata 分区
west flash -p COM3 --userdata path/to/userdata.bin --userdata-addr 0x00A00000
```

> 依赖 `download/mpcli/mpcli.exe`，烧录完成后会有 `[DONE]` 或 `[FAILED]` 提示。

### `west size`

调用 `arm-none-eabi-size` 显示各 section 内存占用，以及 MP binary 大小。

```bash
west size
```

示例输出：

```text
ELF: .../gcc/bin/RTL8773E.hmi_dashboard_src/honeygui_src.elf

section              size      addr
.text              123456  0x00100000
.data                1234  0x00200000
.bss                 5678  0x00201000
...

MP binary : 126,976 bytes  (124.0 KB)
```

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
