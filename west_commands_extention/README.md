# West extension commands

This directory contains the West extension commands customised for the RTL8773G HMI project.

## Available commands

### `west info`
Shows project version and build information.

```bash
west info
```

**Sample output:**
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
Cleans all build artifacts and cache files.

```bash
# interactive clean (prompts for confirmation)
west clean-all

# forced clean (no confirmation prompt)
west clean-all -f
west clean-all --force
```

**Notes:**
- Deletes the `build/` directory
- Deletes all `build-*` directories (such as `build-rtl8773g`)
- Asks for confirmation when `-f` is not given

---

### `west flash-jlink`
Flashes firmware to the RTL8773G using the J-Link debugger.

```bash
# use the default hex file (build/zephyr/zephyr.hex)
west flash-jlink

# specify a custom hex file
west flash-jlink --hex path/to/custom.hex
```

**Notes:**
- This is a template command; it needs to be customised for the actual J-Link setup
- Looks for `build/zephyr/zephyr.hex` by default
- Edit the J-Link parameters in `commands.py` before real use

---

### `west gui-demo`
Builds the HMI application with the GUI demo feature enabled.

```bash
# build for the default board (rtl8773g)
west gui-demo

# build for a specific board
west gui-demo -b rtl8773g

# force a rebuild (pristine build)
west gui-demo --pristine
```

**Notes:**
- Automatically adds the `-DCONFIG_GUI_DEMO=y` build option
- Flashes with `west flash` after the build completes
- Use `--pristine` for a full rebuild

---

## How to add a new command

1. **Edit `west-commands.yml`** and add the new command definition:

```yaml
- name: my-command
  class: MyCommand
  help: my custom command description
```

2. **Implement the command class in `commands.py`**:

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
        # add command arguments
        parser.add_argument('--option', help='option description')
        return parser

    def do_run(self, args, unknown_args):
        # implement the command logic
        log.inf('run my command')
```

3. **Test the new command**:

```bash
west my-command
```

## File overview

- **`west-commands.yml`**: West command configuration file, defining all extension commands
- **`commands.py`**: Python script containing the implementations of all commands
- **`README.md`**: this file, explaining how to use these commands

## References

- [West Extension Commands Documentation](https://docs.zephyrproject.org/latest/develop/west/extensions.html)
- [West API Reference](https://docs.zephyrproject.org/latest/develop/west/west-apis.html)
