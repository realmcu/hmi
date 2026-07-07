# West Extension Commands

Custom West commands for the RTL8773E Dashboard project (GCC/CMake builds only — for
MDK projects, build directly in Keil).

## Available Commands

### `west info`

Show workspace path, build status, and output ELF file.

```bash
west info
```

### `west build`

Wraps `cmake configure` + `ninja build` so you don't have to type the full command
every time.

```bash
# Default: source GUI + bank0 (equivalent to -m src_bank0)
west build

# Full 4-mode matrix: <gui>_<bank>
west build -m src_bank0   # Source GUI, slot A (default)
west build -m src_bank1   # Source GUI, slot B
west build -m lib_bank0   # Precompiled libgui.a, slot A (faster iteration)
west build -m lib_bank1   # Precompiled libgui.a, slot B

# Backward-compatible aliases: -m src → src_bank0, -m lib → lib_bank0
west build -m src
west build -m lib

# Clean then rebuild
west build -c

# Parallel build (8 threads)
west build -j 8

# CMake configure only, skip build (for debugging CMake configuration)
west build --configure-only
```

Equivalent manual command:

```powershell
# From sdk/, using src_bank0 as an example
cmake -G Ninja -D kconfig_path=board/evb/hmi_dashboard/gcc/defconfig.RTL8773E.hmi_dashboard_src_bank0 -DIS_CHECK_FLOW=OFF -Dcompile_lib_only=OFF -B build
cmake --build build
```

### `west clean`

Remove the CMake build directory.

> Each mode has its own build subdirectory (`build/<mode>/`, e.g. `build/lib_bank1/`),
> so switching modes never triggers a full rebuild — each mode keeps its own
> incremental cache without interfering with the others.

```bash
# Remove build directories for all modes (the whole build/)
west clean

# Remove the build directory for one mode only
west clean -m lib_bank1

# Also remove the board/evb/hmi_dashboard/bin/ output directory
west clean --all
```

### `west flash`

Calls `gcc/download.bat`, auto-locates the MP binary, and flashes it to the device
over serial.

```bash
# Use download.bat's default port (COM3), default mode = src_bank0
west flash

# Specify a port
west flash -p COM5

# Flash the bank1 image (must match the mode used at west build -m time)
west flash -m src_bank1
west flash -m lib_bank1 -p COM5

# Also flash the userdata partition
west flash -p COM3 --userdata path/to/userdata.bin --userdata-addr 0x00A00000
```

> `-m` determines which `bin/RTL8773E.hmi_dashboard_<mode>/` directory to pull
> `dashboard_<bank>_MP-*.bin` from. Depends on `download/mpcli/mpcli.exe`; prints
> `[DONE]` or `[FAILED]` when flashing completes.

### `west size`

Calls `arm-none-eabi-size` to show per-section memory usage and the MP binary size.

```bash
west size
```

Example output:

```text
ELF: .../gcc/bin/RTL8773E.hmi_dashboard_src_bank0/honeygui_src.elf

section              size      addr
.text              123456  0x00100000
.data                1234  0x00200000
.bss                 5678  0x00201000
...

MP binary : 126,976 bytes  (124.0 KB)
```

### `west sync`

**Recommended replacement for `west update`.** Runs three steps in order:

1. **Force-update the manifest repo** (`.manifest/`): `git fetch origin` + `git reset --hard origin/<branch>`
2. **`west update`**: sync all West projects per the latest manifest YAML
3. **Submodule update**: run `git submodule update --init --recursive` for every project with a `.gitmodules`

```bash
west sync

# Any native west update argument can be passed through
west sync --narrow
west sync -o=--depth=1
```

> If the manifest repo is in a detached HEAD state, step 1 skips the reset, prints a
> warning, and does not block the rest of the flow.

## MDK Project

The MDK project uses the Keil project files under `board/evb/hmi_dashboard/mdk/` — it
is unrelated to west. Build and flash directly from the Keil IDE.

## Adding a New Command

1. Register it in `west-commands.yml`:

```yaml
- name: my-command
  class: MyCommand
  help: Command description
```

2. Implement it in `commands.py`:

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

## Reference

- [West Extension Commands](https://docs.zephyrproject.org/latest/develop/west/extensions.html)
