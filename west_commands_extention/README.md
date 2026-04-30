# West 扩展命令

RTL8773E HMI 项目的 West 自定义命令。

## 可用命令

### `west info`

显示项目信息。

```bash
west info
```

## 添加新命令

1. 在 `west-commands.yml` 中注册命令：

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

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(self.name, help=self.help)
        return parser

    def do_run(self, args, unknown_args):
        log.inf('hello')
```

## 参考

- [West Extension Commands](https://docs.zephyrproject.org/latest/develop/west/extensions.html)
