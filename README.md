# HMI eBadge

This repository holds the MDK configuration for the Realtek RTL8773E **HMI eBadge**, used to build the firmware for the HMI eBadge project.

The HMI eBadge firmware runs on the RTL8773E and provides the HoneyGUI graphical interface, Bluetooth connectivity, media streaming, and more.

## Tool requirements

| Tool | Version | Purpose | Installation |
| ---- | ------- | ------- | ------------ |
| scons | none | Build the MDK project | pip install scons==4.4.0 |
| MPPGTool | none | Add the mp-header | Unpack under `RTL8773EP/tool/` |
| flash.exe | none | Firmware flashing tool | Unpack under `RTL8773EP/tool/` |
| mpcli.exe | none | Firmware flashing tool | Unpack under `RTL8773EP/tool/` |

## Quick start

### Build the project
To build the MDK project, run the following command in this directory:
```bash
scons --target=mdk5
```

### Generate the firmware

#### Build APP
Open `./mdk/project.uvprojx` and build; the `APP image` output lands in `./mdk/bin/rtl87x3ep/flash_16M_dualbank/bank0/`.

#### Add the mp-header to the userdata resource file
Locate `./app/designer/build/app_romfs.bin` and add the mp-header with the `MPPGTool` from `RTL8773EP/tool/`.

### Flash the firmware

#### First-time flashing
Copy the `APP image` and `userdata` prepared in the previous step into `RTL8773EP/tool/dist/fw/def_bin/` under the flashing tool.

Open `RTL8773EP/tool/dist/flash.exe`, select the serial port, and click `Build & Download`.

#### Flash only the APP image or userdata
Open `CMD` in `RTL8773EP/tool/dist/` and run the following commands:
Tip: command-line flashing requires bin files without the mp-header.
```bash
# Flash the APP image
mpcli.exe -c [comport] -p -A [address]  -F "relative path" -b 3000000  -M 5 -r -u -d -T RTL87X3EP
# Example
mpcli.exe -c com5 -p -A 0x02098000  -F "./fw/def_bin/app_ns.bin" -b 3000000  -M 5 -r -u -d -T RTL87X3EP

# Flash userdata
mpcli.exe -c [comport] -p -A [address + 0x400]  -F "relative path" -b 3000000  -M 5 -r -u -d -T RTL87X3EP
# Example
mpcli.exe -c com5 -p -A 0x0240f400  -F "./fw/def_bin/app_romfs.bin" -b 3000000  -M 5 -r -u -d -T RTL87X3EP
```
