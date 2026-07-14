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
```

> `-m` determines which `bin/RTL8773E.hmi_dashboard_<mode>/` directory to pull
> `dashboard_<bank>_MP-*.bin` from. Depends on `download/mpcli/mpcli.exe`; prints
> `[DONE]` or `[FAILED]` when flashing completes.
> App only — does not touch the userdata partition; use `west userdata` for that (see below).

### `west userdata`

Prepends a real RTL8773E MP header to a userdata (user_data1) bin and flashes it
standalone via `mpcli` — **the app is never touched**, and no `west build` is required.
Defaults to the designer UI's `src/application/designer/build/app_romfs.bin` (source
file is never modified).

```bash
# Package and flash the designer UI's ROMFS resources (default port COM3,
# address from flash_map.h's USER_DATA1_ADDR)
west userdata

# Specify a different bin / port / address
west userdata path/to/userdata.bin -p COM5 --addr 0x00A00000

# Only add the header and write the record bin — don't touch the serial port
# (useful when no board is connected)
west userdata --package-only
```

> Produces two files: the full MP-tagged `record` (BinID/Version/PartNumber included,
> saved next to the source file for production traceability) and a transient
> `flash_ready` copy (the 512-byte MP production header stripped back off, leaving just
> the 1024-byte ctrl header) — the latter is what actually gets written to flash, and is
> deleted afterward.

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

### `west guilib`

Rebuilds both HoneyGUI static libraries from source — armclang (MDK/Keil) and
arm-none-eabi-gcc (GCC lib build modes) — and syncs them into `src/gui_lib/`.

```bash
west guilib
```

Runs both build scripts in turn:

- `lib/armclang/bulidRTL8773E.bat` (Keil armclang toolchain,
  `C:/Keil_v5/ARM/ArmCompilerforEmbedded6.22` by default)
- `lib/arm-none-eabi-gcc/bulidRTL8773E.bat` (`arm-none-eabi-gcc` from PATH,
  else `C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/13.2 Rel1/bin`)

then copies each build's output into `board/evb/hmi_dashboard/src/gui_lib/`:

- `lib/armclang/install/lib/gui.lib` → `src/gui_lib/armclang/gui.lib`
- `lib/arm-none-eabi-gcc/install/lib/libgui.a` → `src/gui_lib/gcc/libgui.a`
- `install/include/*` (identical on both sides) → `src/gui_lib/include/`

> All three destination directories (`armclang/`, `gcc/`, `include/`) are
> removed and recreated before copying, so files deleted upstream don't linger
> as stale leftovers.

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
