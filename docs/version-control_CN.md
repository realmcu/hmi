# 版本号管控

## 只改 `version_cfg.txt`

固件版本号由工程根目录的 `version_cfg.txt` 决定。`version.h` 是构建时生成物（已在
`.gitignore` 中），手工修改会在下一次构建时被覆盖。

## 版本号流向哪里

| 载体 | 内容 | 生成者 |
| --- | --- | --- |
| `version.h` | `VERSION_MAJOR/MINOR/REVISION/BUILDNUM`、`VERSION_GCID`、`COMMIT`、`CUSTOMER_NAME` / `CN_1..CN_8`、构建时间、构建人 | `tools/version_gen.sh` |
| 产物文件名 | `dashboard_<模式>_MP-v<主>.<次>.<修订>.<构建号>-<gcid>-<md5>.bin`，例如 `dashboard_src_bank0_MP-v1.0.0.0-c2e4a1b7-<md5>.bin` | after-build 的 `md5_generate.sh` 读取 `version.h` |
| `mdk/mp.ini` 的 `Version=` | 写进 512 字节 MP header，OTA / 升级工具按它比较版本 | `SYNC_MP_INI = 1` 时由脚本同步（本工程默认开启） |

需要注意：本工程源码并不 include `version.h`，它只影响产物命名与构建信息记录；
设备上报、升级校验读的是 MP header 里的 `Version`。

**OTA 升级只认 MP header 的 `Version`。** 升级工具拿新镜像的 MP header 版本与设备当前版本
比较，`version.h` 不参与。所以只改 `version_cfg.txt` 而不同步 `mp.ini` 时，OTA 会认为版本
没变、不触发升级。本工程默认 `SYNC_MP_INI = 1`，构建时把 `mdk/mp.ini` 的 `Version=` 写成
同一个号，避免“文件名 1.0.1.0、设备上报 1.0.0.0”的错位。

两点副作用需要留意：

- `mdk/mp.ini` 是受版本管理的文件，版本号一变它就会出现在 `git status` 里，发版时请一并提交。
- OTA 一般按“新版本不低于当前版本”判断。切换编号体系（例如设备当前 3.14.2.4，新包 1.0.0.0）
  时新号必须更大，否则旧设备会拒绝升级。

## 两条构建链都会执行

| 构建方式 | 执行时机 | 入口 |
| --- | --- | --- |
| Keil MDK | 每次 Build / Rebuild 前的 Before Build | `mdk/project.uvprojx` → User → Run #1 |
| west / cmake | cmake configure 阶段 | `gcc/CMakeLists.txt` |

`version_cfg.txt` 已登记为 CMake configure 依赖，改完后普通 `west build` 会自动重新
configure 并重新生成；Keil 则每次构建都会执行。两者读取同一份配置，产出的版本号一致。

cmake 链路通过 `bash` 调用脚本，并把工作目录固定在工程根目录。机器上找不到 `bash` 时
configure 阶段只会打印一条告警并保留旧的 `version.h`，产物带的仍是上一次的版本号——
Windows 上请安装 Git Bash，或在带 `bash` 的终端里构建。

## 字段说明

| 字段 | 说明 |
| --- | --- |
| `VERSION_MAJOR` / `VERSION_MINOR` / `VERSION_REVISION` | 主 / 次 / 修订号。留空时从 git tag（`<前缀>-vX.Y.Z`）继承；没有 tag 会变成 `0`，因此建议显式填写 |
| `VERSION_BUILDNUM` | 构建号。`auto`（或留空）= 距上一个 tag 的提交数，仓库没有 tag 时为 `0`；也可写死数字，或用环境变量 `BUILD_NUMBER` 覆盖（优先级最高）。超过 2048 直接构建失败，超过 255 只告警——MP header 里这个字段只有一个字节 |
| `CUSTOMER_NAME` | 最长 8 字符，逐字符写入 MP header 的 `CN_1..CN_8`，不足补 `#`；未配置时默认 `sdk-v1` |
| `SYNC_MP_INI` | 置 `1` 时把 `mdk/mp.ini` 的 `Version=` 同步为同一个版本号（本工程已开启，否则 OTA 识别不到升级） |
| `SDK_BASELINE` / `SDK_BASELINE_COMMIT` | 仅作记录：本工程基于哪个 SDK 交付版本 / 提交，便于问题回溯 |

## 常见场景

升修订号（1.0.0.0 → 1.0.1.0）：

```ini
VERSION_MAJOR    = 1
VERSION_MINOR    = 0
VERSION_REVISION = 1
VERSION_BUILDNUM = auto
```

CI 用流水线号做构建号：

```bash
BUILD_NUMBER=137 west build
```

定制客户名（会进 MP header）：

```ini
CUSTOMER_NAME    = acme
```

## 如何确认板子上跑的是哪个包

串口没有版本横幅：本工程没有任何源文件 include `version.h`，版本号只存在于镜像文件名和
MP header 里。要确认设备当前版本，用升级工具读取 MP header 的 `Version=`，或与当初烧录的
镜像的 MP header 比对。

如果需要一串开机打印，可在 `main.c` 里显式包含生成的 `version.h`（文件在工程根目录，
写 `#include "../version.h"` 即可，两条构建链都不需要额外配 include 路径），启动时打印
`VERSION_BUILD_STR` / `COMMIT`。

## 行为约定

- 版本字段没有变化时脚本不重写 `version.h`，构建时间戳保留，避免无意义的重新编译。
- 源码包（非 git 仓库）交付时，`VERSION_GCID` / `COMMIT` 退化为 `0x00000000` /
  `000000000000`，构建照常进行，不影响版本号本身。
- git tag 只是版本字段为空时的兜底。本工程没有发布 tag，留空会静默变成 `0`，
  三个版本号请始终写在 `version_cfg.txt` 里。
- 若改用 tag 管版本，tag 格式为 `<前缀>-v<主>.<次>.<修订>`，例如
  `dashboard-v1.0.0`；tag 之后的每次提交都会让 `VERSION_BUILDNUM` 自动加一。
