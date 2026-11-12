# 版本号管控

## 只改 `version_cfg.txt`

固件版本号由工程根目录的 `version_cfg.txt` 决定。`version.h` 是构建时生成物（已在
`.gitignore` 中），手工修改会在下一次构建时被覆盖。

## 版本号流向哪里

| 载体 | 内容 | 生成者 |
| --- | --- | --- |
| `version.h` | `VERSION_MAJOR/MINOR/REVISION/BUILDNUM`、`VERSION_GCID`、`COMMIT`、构建时间、构建人 | `tools/version_gen.sh` |
| 产物文件名 | `dashboard_bank0-v3.14.2.4-cee6624a-<md5>.bin` | after-build 的 `md5_generate.sh` 读取 `version.h` |
| `mdk/mp.ini` 的 `Version=` | 写进 512 字节 MP header，OTA / 升级工具按它比较版本 | `SYNC_MP_INI = 1` 时由脚本同步（本工程默认开启） |

需要注意：本工程源码并不 include `version.h`，它只影响产物命名与构建信息记录；
设备上报、升级校验读的是 MP header 里的 `Version`。

**OTA 升级只认 MP header 的 `Version`。** 升级工具拿新镜像的 MP header 版本与设备当前版本
比较，`version.h` 不参与。所以只改 `version_cfg.txt` 而不同步 `mp.ini` 时，OTA 会认为版本
没变、不触发升级。本工程默认 `SYNC_MP_INI = 1`，构建时把 `mdk/mp.ini` 的 `Version=` 写成
同一个号，避免“文件名 3.14.2.4、设备上报 1.0.0.0”的错位。

两点副作用需要留意：

- `mdk/mp.ini` 是受版本管理的文件，版本号一变它就会出现在 `git status` 里，发版时请一并提交。
- OTA 一般按“新版本不低于当前版本”判断。切换编号体系（例如设备当前 1.0.0.0，新包 3.14.2.4）
  时新号必须更大，否则旧设备会拒绝升级。

## 两条构建链都会执行

| 构建方式 | 执行时机 | 入口 |
| --- | --- | --- |
| Keil MDK | 每次 Build / Rebuild 前的 Before Build | `mdk/project.uvprojx` → User → Run #1 |
| west / cmake | cmake configure 阶段 | `gcc/CMakeLists.txt` |

`version_cfg.txt` 已登记为 CMake configure 依赖，改完后普通 `west build` 会自动重新
configure 并重新生成；Keil 则每次构建都会执行。两者读取同一份配置，产出的版本号一致。

## 字段说明

| 字段 | 说明 |
| --- | --- |
| `VERSION_MAJOR` / `VERSION_MINOR` / `VERSION_REVISION` | 主 / 次 / 修订号。留空表示从 git tag（`<前缀>-vX.Y.Z`）继承 |
| `VERSION_BUILDNUM` | 构建号。`auto` = 距上一个 tag 的提交数；也可写死数字，或用环境变量 `BUILD_NUMBER` 覆盖。上限 4095，超出会直接构建失败 |
| `CUSTOMER_NAME` | 最长 8 字符，逐字符写入 MP header 的 `CN_1..CN_8`，不足补 `#` |
| `SYNC_MP_INI` | 置 `1` 时把 `mdk/mp.ini` 的 `Version=` 同步为同一个版本号（本工程默认开启，否则 OTA 识别不到升级） |
| `SDK_BASELINE` | 仅作记录：本工程基于哪个 SDK 交付版本，便于问题回溯 |

## 常见场景

升修订号（3.14.2 → 3.14.3）：

```ini
VERSION_MAJOR    = 3
VERSION_MINOR    = 14
VERSION_REVISION = 3
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

## 开机 log

`src/application/main.c` 在 `main()` 开头打印构建时的版本，串口 log 第一行附近可以看到：

```
APP version 3.14.2.4 commit cee6624ace78
```

这是编译期固化的值，与同一次构建写进 MP header 的版本一致，用来确认板子上跑的到底是哪个包。
（`version.h` 用的是相对包含 `../version.h`，不依赖任何 include 路径配置。）

## 行为约定

- 版本字段没有变化时脚本不重写 `version.h`，构建时间戳保留，避免无意义的重新编译。
- 源码包（非 git 仓库）交付时，`VERSION_GCID` / `COMMIT` 退化为 `0x00000000` /
  `000000000000`，构建照常进行，不影响版本号本身。
- 若沿用 git tag 管版本，tag 格式为 `<前缀>-v<主>.<次>.<修订>`，例如
  `dashboard-v3.14.2`；tag 之后的每次提交都会让 `VERSION_BUILDNUM` 自动加一。
