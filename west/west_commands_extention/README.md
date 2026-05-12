# West 扩展命令

这个目录包含了为 RTL8773G HMI 项目定制的 West 扩展命令。

## 可用命令

### `west info`
显示项目版本和构建信息。

```bash
west info
```

**输出示例：**
```
RTL8773G HMI Project Information
==================================================
Project: RTL8773G HMI Application
Owner: howie_wang
Zephyr Version: realtek-main-v3.7

Workspace: /home/user/workspace/hmi-project
Application Path: zephyrproject/realtek-app/applications/hmi
==================================================
```

---

### `west clean-all`
清理所有构建产物和缓存文件。

```bash
# 交互式清理（会提示确认）
west clean-all

# 强制清理（不提示确认）
west clean-all -f
west clean-all --force
```

**说明：**
- 删除 `build/` 目录
- 删除所有 `build-*` 目录（如 `build-rtl8773g`）
- 不使用 `-f` 参数时会要求确认

---

### `west flash-jlink`
使用 J-Link 调试器烧录固件到 RTL8773G。

```bash
# 使用默认的 hex 文件（build/zephyr/zephyr.hex）
west flash-jlink

# 指定自定义 hex 文件
west flash-jlink --hex path/to/custom.hex
```

**说明：**
- 这是一个模板命令，需要根据实际的 J-Link 配置进行定制
- 默认查找 `build/zephyr/zephyr.hex` 文件
- 实际使用前请修改 `commands.py` 中的 J-Link 参数

---

### `west gui-demo`
构建启用了 GUI 演示功能的 HMI 应用。

```bash
# 为默认板子（rtl8773g）构建
west gui-demo

# 为特定板子构建
west gui-demo -b rtl8773g

# 强制重新构建（pristine build）
west gui-demo --pristine
```

**说明：**
- 自动添加 `-DCONFIG_GUI_DEMO=y` 编译选项
- 构建完成后使用 `west flash` 烧录
- 使用 `--pristine` 进行完全重新构建

---

## 如何添加新命令

1. **编辑 `west-commands.yml`**，添加新命令定义：

```yaml
- name: my-command
  class: MyCommand
  help: 我的自定义命令描述
```

2. **在 `commands.py` 中实现命令类**：

```python
class MyCommand(WestCommand):
    def __init__(self):
        super().__init__(
            'my-command',
            'short help text',
            'detailed description'
        )

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(
            self.name,
            help=self.help,
            description=self.description
        )
        # 添加命令参数
        parser.add_argument('--option', help='选项说明')
        return parser

    def do_run(self, args, unknown_args):
        # 实现命令逻辑
        log.inf('执行我的命令')
```

3. **测试新命令**：

```bash
west my-command
```

## 文件说明

- **`west-commands.yml`**: West 命令配置文件，定义了所有扩展命令
- **`commands.py`**: Python 脚本，包含所有命令的实现
- **`README.md`**: 本文件，说明如何使用这些命令

## 参考资料

- [West Extension Commands Documentation](https://docs.zephyrproject.org/latest/develop/west/extensions.html)
- [West API Reference](https://docs.zephyrproject.org/latest/develop/west/west-apis.html)
