# 常见问题

本文汇总 RTL87X3G HMI SDK 环境、Chargebox application 构建和 App 烧录中的常见问题。

> [返回项目首页](../README_CN.md)

## 1. `west` 命令不存在

确认已激活 Python 虚拟环境，并重新安装：

```bash
python -m pip install --upgrade west
west --version
```

## 2. 找不到 Zephyr SDK toolchain 或 ARM compiler

确认已经下载 Zephyr SDK toolchain 0.16.9，并执行其中的 `setup.cmd` 或 `setup.sh`。

系统中安装了多个版本时，设置 `ZEPHYR_TOOLCHAIN_VARIANT` 和 `ZEPHYR_SDK_INSTALL_DIR`，然后重新执行 pristine build。详细步骤参见[开发环境与 SDK 获取](getting-started_CN.md)。

## 3. 后处理提示找不到 `mp.ini` 或 `VERSION`

通常是从 workspace 根目录构建但没有指定 application 的 build 目录。进入 Chargebox application 后重新执行：

```bash
cd zephyrproject/realtek-app/applications/chargebox
west build -b rtl87x3g_evb -p always
```

如果必须从 workspace 根目录构建，应显式指定 application 的 source 和 build 目录，参见[编译、烧录与日志](build-and-flash_CN.md)。

## 4. `mpcli` 命令不存在

在 workspace 根目录重新执行当前平台的安装脚本。

**Windows PowerShell：**

```powershell
powershell -ExecutionPolicy Bypass -File zephyrproject\realtek-app\tools\mpcli\setup.ps1
mpcli --help
```

**Ubuntu：**

```bash
source zephyrproject/realtek-app/tools/mpcli/setup.sh
mpcli --help
```

## 5. 烧录握手超时

依次检查：

1. 下载串口名称是否正确；
2. TX/RX 是否交叉连接，GND 是否共地；
3. `P2_0` 是否已拉低并在此后复位设备；
4. 串口是否被日志终端或其他程序占用；
5. `mpcli` 同目录下的 `fw/` 和 `config/` 是否完整；
6. 固件和基础镜像是否匹配目标 RTL87X3G 硬件版本。

## 6. 修改配置后结果没有变化

执行完整清理构建：

```bash
west build -b rtl87x3g_evb -p always
```

还可以检查本次构建生成的 `bin/app.config` 和 `bin/app.dts`，确认 Kconfig 与 Devicetree 修改是否生效。

## 7. 只有 App 镜像能否烧录空白芯片

不能。`bin/app.bin` 只更新 App 分区。

空白芯片还需要 Boot Patch、Upperstack、System Patch、DSP、System Config、App Config 等与当前 SDK release 匹配的基础镜像。请使用正式 release 提供的完整烧录包，不要从其他版本或其他 RTL87X3G 产品中拼接镜像。

## 8. 直接执行 `west flash` 时为什么调用 J-Link

`rtl87x3g_evb` 的默认 flash runner 是 J-Link。使用串口 `mpcli` 时必须显式指定：

```bash
west flash --runner mpcli --port <serial-port> \
  --file bin/app.bin \
  --bin-address 0x7009E000
```

参数和硬件连接说明参见[编译、烧录与日志](build-and-flash_CN.md)。

## 9. 烧录成功但设备不能启动

检查：

- 是否烧录了 `bin/app.bin`，而不是未经 Realtek App header 后处理的 `build/zephyr/zephyr.bin`；
- App 地址是否与当前 `flash_map.h` 一致；
- 基础镜像、App 和硬件版本是否匹配；
- App 是否超过当前分区容量；
- 空白芯片是否已经完成整机镜像烧录。

## 10. 打开串口终端后设备复位或没有日志

确认串口参数为 `2000000, 8-N-1`，并关闭 RTS/CTS 和 DTR/DSR 硬件流控。某些 USB-to-UART 连接会通过控制线影响设备复位。

烧录口和日志口共用同一串口时，确保同一时刻只有一个程序占用该端口。
