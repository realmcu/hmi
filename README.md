# HMI eBadge

本仓库是 Realtek RTL8773E **HMI eBadge** 的 MDK 配置仓库，用于编译生成 HMI eBadge 项目的固件。

基于 RTL8773E 的 HMI eBadge 固件，提供 HoneyGUI 图形界面、蓝牙连接、流媒体播放等功能。

## 工具要求

| 工具 | 版本要求 | 用途 | 安装 |
| ---- | -------- | ---- | ---- |
| scons | 无 | 构建 MDK 工程 | pip install scons==4.4.0 |
| MPPGTool | 无 | 添加 mp-header | `RTL8773EP/tool/`下解压 |
| flash.exe | 无 | 烧录固件工具 | `RTL8773EP/tool/`下解压 |
| mpcli.exe | 无 | 烧录固件工具 | `RTL8773EP/tool/`下解压 |

## 快速开始

### 构建工程
如果需要构建 MDK 工程，在该目录下执行以下命令：
```bash
scons --target=mdk5
```

### 生成固件

#### Build APP
打开 `./mdk/project.uvprojx` 编译即可，生成 `APP image` 文件目录 `./mdk/bin/rtl87x3ep/flash_16M_dualbank/bank0/ ` 。

#### 资源文件 userdata 添加 mp-header
找到 `./app/designer/build/app_romfs.bin`, 使用 `RTL8773EP/tool/` 中的 `MPPGTool` 工具添加 mp-header。

### 烧录固件

#### 第一次烧录
将第三点准备好的 `APP image` 以及 `userdata` 复制到烧录工具下 `RTL8773EP/tool/dist/fw/def_bin/`。

打开 `RTL8773EP/tool/dist/flash.exe`，选择串口后点击 `打包下载(Build & Download)` 即可。

#### 单独烧录 APP image 或 userdata
`RTL8773EP/tool/dist/`中打开 `CMD`, 执行以下命令：
Tip: 命令行烧录时需要不带 mp-headr 的 bin 文件。
```bash
# 烧录 APP image
mpcli.exe -c [comport] -p -A [address]  -F "relative path" -b 3000000  -M 5 -r -u -d -T RTL87X3EP
# Example
mpcli.exe -c com5 -p -A 0x02098000  -F "./fw/def_bin/app_ns.bin" -b 3000000  -M 5 -r -u -d -T RTL87X3EP

# 烧录 userdata
mpcli.exe -c [comport] -p -A [address + 0x400]  -F "relative path" -b 3000000  -M 5 -r -u -d -T RTL87X3EP
# Example
mpcli.exe -c com5 -p -A 0x0240f400  -F "./fw/def_bin/app_romfs.bin" -b 3000000  -M 5 -r -u -d -T RTL87X3EP
```
