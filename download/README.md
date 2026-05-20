# download

Flash firmware to RTL87X3EP via UART.

## Usage

```bat
download.bat [COM] <APP_BIN> [USERDATA_FILE USERDATA_ADDR]
```

| Parameter | Description | Default |
| --------- | ----------- | ------- |
| `COM` | Serial port | `COM3` (edit `DEFAULT_COM` in script) |
| `APP_BIN` | App image path (required) | — |
| `USERDATA_FILE` | Userdata file path (optional) | — |
| `USERDATA_ADDR` | Userdata flash address (required if `USERDATA_FILE` is set) | — |

## Examples

```bat
rem Flash app
download.bat COM3 ..\dashboard\gcc\bin\RTL8773E.hmi_dashboard_src\honeygui_src_MP-1234.bin

rem Flash app + userdata
download.bat COM3 path\to\app.bin path\to\resource.bin 0x704D1000
```

> **Project wrappers** (e.g. `dashboard\gcc\download.bat`) auto-resolve the app bin
> and delegate here — use those for day-to-day flashing.

## Flash Address

The app address is parsed at runtime from the SDK flash_map.h — no hardcoding needed:

```text
sdk\bin\rtl87x3ep\flash_map_config\16M\flash_16M\flash_map.h
```

## Directory Structure

```text
download/
├── download.bat
└── mpcli/          Realtek mpcli v4.0.0.3 (RTL87X3EP)
    ├── mpcli.exe
    ├── config/
    └── fw/RTL87X3EP/
```
