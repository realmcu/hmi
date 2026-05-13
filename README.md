# RTL8773E HMI Application

RTL8773E HMI Application using West for multi-repository management.

## Project Structure

The West workspace consists of two repositories, with hmi as the manifest repo nested inside the honeycomb SDK:

```
workspace/                                          # West workspace root (.west/ is here)
├── .west/                                          # West configuration
└── honeycomb/                                      # [project] Release SDK repository
    └── sdk/
        ├── bin/
        ├── board/
        │   └── evb/
        │       └── hmi/                            # [self] This repository (manifest repo)
        │           ├── manifest/
        │           │   └── rtl8773e-hmi.yml
        │           ├── west_commands_extention/
        │           │   ├── west-commands.yml
        │           │   └── commands.py
        │           └── README.md
        ├── config/
        ├── doc/
        ├── src/
        └── ...
```

## Getting the Code

```bash
# Initialize West workspace
# Replace <your_username> with your Gerrit username, e.g., howie_wang
# ~/workspace/hmi-project can be replaced with your desired directory
# --mr rtl8773e specifies using the rtl8773e branch
west init -m ssh://<your_username>@cn4soc.rtkbf.com:29418/HoneyRepo/hmi --mf manifest/rtl8773e-hmi.yml --mr rtl8773e ~/workspace/hmi-project

cd ~/workspace/hmi-project

west update
```

### Using Existing Repository to Build West Workspace

If you already have a honeycomb SDK repository locally (e.g., previously cloned release-crb-3.14.0), you can reuse it directly without re-downloading:

```bash
# 1. Create workspace directory
mkdir ~/workspace/hmi-project
cd ~/workspace/hmi-project

# 2. Copy (or move) the existing honeycomb repository to the workspace
# Make sure the .git directory is at honeycomb/.git
cp -r /path/to/your/existing/honeycomb ~/workspace/hmi-project/honeycomb

# 3. Clone hmi (manifest repo) to the specified location inside honeycomb
git clone ssh://<your_username>@cn4soc.rtkbf.com:29418/HoneyRepo/hmi honeycomb/sdk/board/evb/hmi

# 4. Initialize West
# Note: Must use the manual configuration file method, cannot use west init -l (will cause duplicate clone)
mkdir .west
cat > .west/config << EOF
[manifest]
path = honeycomb/sdk/board/evb/hmi
file = manifest/rtl8773e-hmi.yml
EOF

# 5. Update workspace
west update
```

> **Note:** Step 2 reuses the existing honeycomb SDK to avoid re-cloning the large repository. When West detects an existing `.git` directory under `honeycomb/`, it will reuse it and only perform fetch and checkout. After step 3 cloning hmi, you need to manually switch to the rtl8773e branch. If you cannot access the remote repository at all, you can use `west update --fetch=never` to skip the fetch step.

## Dependencies

| Repository | Description |
|------------|-------------|
| release-crb-3.14.0 | RTL8773E Release SDK |

## Build Methods

### MDK Build

```bash
cd board/evb/hmi_app/dashboard

# Modify menu_config.h to select configuration
# - GUI build mode: source code or precompiled library
# - Demo selection
# - Feature configuration

# Use scons to generate MDK project
scons

# Generated project files are in mdk/ directory
# Open mdk/project.uvprojx with Keil MDK to build
```

### GCC Build

HMI Dashboard supports GCC compilation with two modes:

#### 1. Source Code Build Mode

Build from GUI sub-repository source code, supporting different demo selection:

```bash
cd honeycomb/sdk

# Create build directory
mkdir -p build/dashboard_src
cd build/dashboard_src

# Configure (using source code defconfig)
cmake ../.. -Dkconfig_path=board/evb/hmi_app/dashboard/gcc/defconfig.RTL8773E.16M_bank0_src

# Build
cmake --build . --target honeygui
```

#### 2. Precompiled Library Mode

Link with precompiled `libgui.a`:

```bash
cd honeycomb/sdk

mkdir -p build/dashboard_lib
cd build/dashboard_lib

cmake ../.. -Dkconfig_path=board/evb/hmi_app/dashboard/gcc/defconfig.RTL8773E.16M_bank0_lib

cmake --build . --target honeygui
```

For detailed instructions, see [gcc/README.md](dashboard/gcc/README.md).

## Configuration Mapping

| MDK (menu_config.h) | GCC (defconfig) |
|---------------------|-----------------|
| `CONFIG_REALTEK_HONEYGUI_BUILD_MODE = 1` | `CONFIG_REALTEK_HONEYGUI_BUILD_SRC=y` |
| `CONFIG_REALTEK_HONEYGUI_BUILD_MODE = 0` | `CONFIG_REALTEK_HONEYGUI_BUILD_LIB=y` |
| `CONFIG_REALTEK_HONEYGUI_DEMO_SELECT` | Kconfig choice menu selection |

## Output Files

Build outputs are located at `board/evb/hmi_app/dashboard/bin/<config_name>/`:

- `honeygui_bank0.elf` - ELF executable file
- `honeygui_bank0.hex` - HEX firmware
- `honeygui_bank0_MP.bin` - Signed BIN firmware
