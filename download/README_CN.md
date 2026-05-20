# download

通过 UART 将固件烧录到 RTL87X3EP 芯片。

## 用法

```bat
download.bat [COM] <APP_BIN> [USERDATA_FILE USERDATA_ADDR]
```

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `COM` | 串口号 | `COM3`（可在脚本顶部 `DEFAULT_COM` 修改） |
| `APP_BIN` | App 镜像路径（必填） | — |
| `USERDATA_FILE` | Userdata 文件路径（可选） | — |
| `USERDATA_ADDR` | Userdata 烧录地址（`USERDATA_FILE` 存在时必填） | — |

## 示例

```bat
rem 烧录 app
download.bat COM3 ..\dashboard\gcc\bin\RTL8773E.hmi_dashboard_src\honeygui_src_MP-1234.bin

rem 同时烧录 app + userdata
download.bat COM3 path\to\app.bin path\to\resource.bin 0x704D1000
```

> **工程专用包装脚本**（如 `dashboard\gcc\download.bat`）会自动定位 app bin
> 并调用此脚本，日常烧录建议使用工程包装脚本。

## Flash 地址

App 地址从 SDK flash_map.h 动态读取，无需手动修改：

```text
sdk\bin\rtl87x3ep\flash_map_config\16M\flash_16M\flash_map.h
```

## 目录结构

```text
download/
├── download.bat
└── mpcli/          Realtek mpcli v4.0.0.3（RTL87X3EP）
    ├── mpcli.exe
    ├── config/
    └── fw/RTL87X3EP/
```
