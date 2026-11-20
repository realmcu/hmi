# West Extension Commands

This directory contains the West extension commands customized for the RTL8773G HMI project.

## Available Commands

### `west info`
Displays the project version and build information.

```bash
west info
```

**Example output:**
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
# Interactive clean (prompts for confirmation)
west clean-all

# Forced clean (no confirmation prompt)
west clean-all -f
west clean-all --force
```

**Notes:**
- Deletes the `build/` directory
- Deletes all `build-*` directories (such as `build-rtl8773g`)
- Asks for confirmation when the `-f` option is not specified

---

### `west flash-jlink`
Flashes firmware to the RTL8773G using the J-Link debugger.

```bash
# Use the default hex file (build/zephyr/zephyr.hex)
west flash-jlink

# Specify a custom hex file
west flash-jlink --hex path/to/custom.hex
```

**Notes:**
- This is a template command and must be customized for your actual J-Link setup
- Looks for the `build/zephyr/zephyr.hex` file by default
- Modify the J-Link parameters in `commands.py` before actual use

---

### `west gui-demo`
Builds the HMI application with the GUI demo feature enabled.

```bash
# Build for the default board (rtl8773g)
west gui-demo

# Build for a specific board
west gui-demo -b rtl8773g

# Force a rebuild (pristine build)
west gui-demo --pristine
```

**Notes:**
- Automatically adds the `-DCONFIG_GUI_DEMO=y` build option
- Flashes with `west flash` after the build completes
- Use `--pristine` for a completely fresh build

---

## How to Add a New Command

1. **Edit `west-commands.yml`** and add the new command definition:

```yaml
- name: my-command
  class: MyCommand
  help: My custom command description
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
        # Add command arguments
        parser.add_argument('--option', help='option description')
        return parser

    def do_run(self, args, unknown_args):
        # Implement the command logic
        log.inf('Execute my command')
```

3. **Test the new command**:

```bash
west my-command
```

## File Descriptions

- **`west-commands.yml`**: West command configuration file, defines all extension commands
- **`commands.py`**: Python script, contains the implementation of all commands
- **`README.md`**: This file, explains how to use these commands

## References

- [West Extension Commands Documentation](https://docs.zephyrproject.org/latest/develop/west/extensions.html)
- [West API Reference](https://docs.zephyrproject.org/latest/develop/west/west-apis.html)
