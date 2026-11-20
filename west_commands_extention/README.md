# CLAW West Extension Commands

This directory provides West
extension commands dedicated to the `realtek-app/applications/claw/RustMcuClaw/mcu` project, saving you from typing `-b` and the application path by hand every time.

File structure:

```
applications/claw/
├── manifest/
│   └── rtl8773g-zephyr-claw.yml        # Project manifest, references this directory's west-commands.yml
└── west_commands_extention/
    ├── west-commands.yml              # Registers the three claw-* commands
    ├── commands.py                    # Command implementations (HmiBuild / HmiFlash / HmiClean)
    └── README.md                      # This document
```

Equivalent raw commands (still available at any time):

```powershell
west build -b rtl87x3g_watch/rtl8783gbf `
    realtek-app\applications\claw\RustMcuClaw\mcu
west flash -d realtek-app\applications\claw\RustMcuClaw\mcu\build
```

## 4. Defaults

The three defaults are centralized at the top of `commands.py`; modify them as needed:

| Variable | Default |
| --- | --- |
| `_APP_DIR`           | `applications/claw/RustMcuClaw/mcu` |
| `_DEFAULT_BOARD`     | `rtl87x3g_watch/rtl8783gbf` |
| `_DEFAULT_BUILD_DIR` | `<_APP_DIR>/build` |

## 5. FAQ

**`FATAL ERROR: no west workspace found ...`**
The current directory is not inside an initialized west workspace, or `.west/config` was deleted.
Re-run `west init -l` as described in section 1.

**`west: error: argument <command>: invalid choice: 'claw-build'`**
This means the manifest does not point to this directory's `west-commands.yml`. Common causes:

1. `west config manifest.path` is not `realtek-app/applications/claw/manifest`.
2. The manifest was edited manually but `west update` was not run again.

**Rust target not found**
Install the thumbv8 target before the first build:

```powershell
rustup target add thumbv8m.main-none-eabihf
```
