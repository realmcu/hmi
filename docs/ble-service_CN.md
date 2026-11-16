# BLE 服务与导航投屏

空口协议本身以已发布的 **HoneyProtocol** 文档为准
（<https://realmcu.github.io/HoneyProtocol/>，最后更新 2026-11-09）：L0/L1/L2 的帧格式
和每条 L2 命令的字段定义都定义在那里。本文只讲**本固件**在其之上暴露了什么——广播身份、
HMIS GATT 服务、导航投屏会话，以及与规范不一致的地方。

本文对应的代码：

| 范围 | 文件 |
| --- | --- |
| 广播 / 扫描响应 | `src/hmi_rtk_bt/hmi_ble/hmi_ble_gap_init.c` |
| HMIS 服务与特征 | `src/hmi_rtk_bt/hmi_ble/private_service/hmi_ctrl_service.c` |
| L0 承载绑定（`0xFFC2` notify） | `src/hmi_rtk_bt/hmi_app/hmi_ble_ctrl.c` |
| L1 帧与 CRC | `src/protocol/hmi_proto.c` |
| L2 路由 | `src/protocol/hmi_l2.c` |
| 投屏会话、JPEG 校验、界面 | `src/hmi_rtk_bt/hmi_ble/private_service/navi_projection_service.c` |
| 通用 L2 命令字 | `src/protocol/hmi_l2.h` |

## 设备对外暴露的服务

| 服务 | UUID | 方向 | 用途 |
| --- | --- | --- | --- |
| HMIS 私有服务 | `484D4953-0000-1000-8000-00805F9B34FB` | 双向 | 控制通道，同时是投屏承载 |
| OTA 服务 | SDK 的 vendor OTA UUID（`ota_add_service`） | 双向 | BLE 通道升级固件 |
| Nordic UART（NUS） | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` | 双向 | 为兼容而注册，当前工程没有模块向它发数据 |
| SPP（BR/EDR） | — | 双向 | `src/ota/` 使用的升级通道 |

这里只展开 HMIS 服务。BLE 与 BR/EDR 同时开启，BR/EDR 侧在 `src/hmi_rtk_bt/hmi_app/`
下实现了 A2DP（sink 与 source）、AVRCP、HFP、SPP、PAN。

## 广播与扫描响应

`ADV_IND` 正好占满 31 字节，设备名放在扫描响应里。广播间隔固定为
`320 × 0.625 ms = 200 ms`（最小 = 最大）。

| 报文 | 字段 | 字节数 |
| --- | --- | --- |
| ADV_IND | Flags：LE 通用可发现，不支持 BR/EDR | 3 |
| ADV_IND | 128-bit 服务 UUID 完整列表：HMIS | 18 |
| ADV_IND | 厂商自定义数据（公司 ID `0x005D` + 6 字节负载） | 10 |
| SCAN_RSP | 完整设备名 `HMI-Dashboard` | 14 |

厂商数据负载布局：

| 偏移 | 字段 | 说明 |
| --- | --- | --- |
| 0 | 负载版本 | `0x01` |
| 1 | 机型 | `0x01` RTL8773G、`0x02` RTL8773E、`0x03` RTL8782 |
| 2 | 能力位 | bit0 投屏、bit1 已绑定、bit2 充电中 |
| 3 | 电量 | 百分比，运行时更新 |
| 4..5 | 设备短 ID | BD 地址低 16 bit，小端 |

配对使用 LE bonding，IO 能力为 `NoInputNoOutput`。链路建立后设备作为 slave 主动发起
MTU 交换（`GAP_PARAM_SLAVE_INIT_GATT_MTU_REQ`）。

## HMIS 私有服务

与规范 §2.2.1 一致。

| 特征 | UUID | 属性 | 内容 |
| --- | --- | --- | --- |
| Command | `0xFFC1` | Write / Write No Rsp | 手机 → 设备，最长 512 字节 |
| Event | `0xFFC2` | Notify（带 CCCD） | 设备 → 手机，一次 notify 一个 L1 帧 |
| Status | `0xFFC3` | Read | 设备状态，最长 32 字节，user description 为 `HMIS Read` |

手机必须先打开 `0xFFC2` 的 CCCD，设备才会发通知。协议层面 write 与 notify 是对称的数据
通道，承载相同的 L2 帧；read 特征只用于读状态，不承载数据流。

## 协议分层

所有多字节字段均为**大端**。

- **L0** —— GATT 承载。本设备就是上面的 HMIS 特征：手机写 `0xFFC1`，设备 notify
  `0xFFC2`（`hmi_ble_ctrl.c` 中的 `hmi_l1_carrier_tx()`）。
- **L1** —— 8 字节头 + 负载，全固件只有一份实现（`src/protocol/hmi_proto.c`）：

| 偏移 | 长度 | 字段 |
| --- | --- | --- |
| 0 | 1 | 魔数 `0xAB` |
| 1 | 1 | bit7 保留、bit6 请求 ACK、bit5 错误、bit4 ACK、bit3..0 版本（`0`） |
| 2..3 | 2 | 负载长度 |
| 4..5 | 2 | CRC-16/ARC（初值 `0x0000`，反射多项式 `0xA001`），**仅覆盖负载**；`"123456789"` 校验值 `0xBB3D` |
| 6..7 | 2 | 序列号 |
| 8..N | N | 负载 |

- **L2** —— 5 字节头 + 值，装在一个 L1 帧里：

| 偏移 | 长度 | 字段 |
| --- | --- | --- |
| 0 | 1 | 命令 |
| 1 | 1 | 版本（高 4 位）/ 保留（低 4 位） |
| 2 | 1 | Key |
| 3..4 | 2 | 值长度 |
| 5..N | N | 值 |

**实现与规范的差异**：规范允许 L1 重组到 65535 字节，且没有规定 L0 上如何分片；本固件
两者都没有做——一个 L1 帧必须落在一次 write/notify 里，投屏还把 L1 负载限制在 248 字节
（含头 250 字节）。手机端必须保证每次写入不超过协商后的 MTU。

## 本固件使用的 L2 命令

`src/protocol/hmi_l2.c` 是路由层：解析 5 字节 L2 头，`0x05` 由它自己应答，应用自管的帧交给
投屏引擎，未命中的命令只记录日志、不回包。目前使用两个命令字：`0x05`（设备信息）与 `0x10`
（画面传输）；`0x11` 已不再使用，规范里它是「点测」。

### `0x05` —— 设备信息（投屏握手）

L2 头为 `05 10`（版本 1）。手机每次连接问一次，就能拿到选分辨率、码率和分块大小所需的全部上限。

| Key | 名称 | 方向 | 值 |
| --- | --- | --- | --- |
| `0x0B` | 设备信息请求 | 手机 → 设备 | 空 |
| `0x2B` | 设备信息应答 | 设备 → 手机 | TLV 列表 |

每个 TLV 为 `id(1) + 长度(2，大端) + 值`；未知 id 直接忽略，因此以后加字段不需要升版本。

| id | 字段 | 长度 | 本固件取值 |
| --- | --- | --- | --- |
| `0x01` | 投屏区域 | 4 | `u16 宽`、`u16 高` —— **410 × 370**，即 Designer UI 里 `gui_stream` 控件的实际几何 |
| `0x02` | JPEG 上限 | 4 | `u32` = **40960** —— 单个 transport 缓冲能装下的**压缩后** JPEG 字节数（池为 5 × 40 KB）；131072 只是编译期硬上限 |
| `0x03` | JPEG 质量 | 1 | **60** —— 手机应按此质量编码，保证 410 × 370 的一帧落在 40 KB 缓冲内 |
| `0x04` | 特性位 | 4 | bit0 投屏、bit1 二维码配对、bit2 设备端校验 CRC32 + JPEG |
| `0x05` | 分块上限 | 2 | 241 字节 |
| `0x06` | 请求帧率 | 1 | **3** —— 设备希望手机采用的采样率 |

应答 34 字节（空口 47 字节），肯定装得进一个 L1 帧。示例（L2 部分）：

```
请求 : 05 10 0B 00 00
应答 : 05 10 2B 00 22
       01 00 04 01 9A 01 72     区域 410 x 370
       02 00 04 00 00 A0 00     JPEG 上限 40960
       03 00 01 3C              建议质量 60
       04 00 04 00 00 00 07     特性位：投屏 + 二维码 + 设备端校验
       05 00 02 00 F1           分块上限 241
       06 00 01 03              请求 3 fps
```

`0x02` 指的是压缩后的文件大小，不是解码后的画面：解码端会按分量平面 + RGB888 一帧去核算，
与 2.5 MB 的 GUI 堆比较（410 × 370 峰值约 0.75 MB），所以 40 KB 的传输上限并不限制解码。

手机以 `0x06` 作为采样率、以 `0x03` 作为编码质量，两者都必须在首帧之前确定（虚拟屏启动时
尺寸与编码参数就固化了）；重连后应重新查询。1 s 内没有应答说明设备是旧固件：沿用本地默认值继续投屏即可，`0x10`
流程完全不变。

### `0x10` —— 文件传输（一帧一个 JPEG 会话）

即规范的 §3.12，本固件把它当作投屏数据通道：每个地图画面作为完整 JPEG 文件发送，下一帧
另起一个会话。

| Key | 名称 | 方向 | 值 |
| --- | --- | --- | --- |
| `0x01` | BEGIN 请求 | 手机 → 设备 | `file_type`(1)、`total_size`(4)、`chunk_size`(2)、`name_len`(1)、`name`(0…n) |
| `0x02` | BEGIN 响应 | 设备 → 手机 | `status`(1)、`chunk_size`(2) |
| `0x03` | DATA | 手机 → 设备 | `seq`(2，从 0 开始)、最多 `chunk_size` 字节 |
| `0x05` | END 请求 | 手机 → 设备 | 整文件 CRC32（4 字节） |
| `0x06` | END 响应 | 设备 → 手机 | `status`(1)、`error_code`(1) |
| `0x07` | ABORT | 双向 | `reason`(1)：`0x00` 用户、`0x01` 错误、`0x02` 超时 |

设备侧规则：

- `file_type` 必须在 `0x01`…`0x03`（图片 / 视频 / 裸数据）；投屏只接受 `0x01`。
- `total_size` 不能为 0，且必须放得进 stream transport 缓冲区（≤128 KB）。
- `chunk_size` 是请求值，范围 1…2048。设备按当前 MTU 回填实际可用值
  ——`MTU − 3 − 8 (L1) − 5 (L2) − 2 (seq)`——手机必须用 BEGIN 响应里的值，而不是自己
  请求的值。MTU 247 时为 229 字节；编译期上限是 241。
- 同一时刻只允许一个会话；再次 BEGIN 会收到 `status = 0x01`（忙）。
- DATA 序号必须逐 1 递增，否则设备发 `ABORT (reason = 0x01)`。
- END 先校验总长度和 CRC32（多项式 `0xEDB88320`，大端，覆盖整个文件），再按 baseline
  JPEG 校验：SOF 宽高必须与首个成功提交的帧一致、`宽 × 高` ≤ 192000、解码后要放得进
  GUI 堆。
- 10 s 无数据则 `ABORT (reason = 0x02)`。

状态码：BEGIN `0x00` 接受、`0x01` 忙、`0x02` 空间不足、`0x03` 类型不支持；
END `0x00` 成功、`0x01` CRC 失败、`0x02` 数据不完整；`error_code` `0x00` 无、`0x01`
写入失败、`0x02` 解码失败。

## 会话时序

```
手机                                    设备
  |-- 0x05 取信息（0x0B）-------------->|
  |<------------ 0x05 信息（0x2B, TLV）--|   区域、JPEG 上限、fps/质量、分块
  |-- 0x10 BEGIN（类型,大小,块,文件名）->|
  |<------------ 0x10 BEGIN_RSP（块长）--|
  |-- 0x10 DATA（seq 0 ...）------------>|   （重复直到 total_size）
  |-- 0x10 END（crc32）----------------->|
  |<------------ 0x10 END_RSP（status）--|   帧提交给 GUI
  |        ... 下一帧 = 新的 BEGIN/DATA/END ...
```

## 二维码配对页

在第一帧成功提交之前，Designer UI 一直停留在二维码页。二维码内容由最终确定的公共地址
生成（`navi_projection_build_qr_url()`）：

```
https://github.com/realmcu/HoneyBox/releases/latest/download/HoneyBox.apk
```

页面附带机型 `RTL8773E` 与序列号 `0529`，手机 App 从二维码内容里取出设备地址。只有确认
有帧提交后才隐藏该页（`navi_gui_stream_active_cb`）——这也是帧被拒时仍停在二维码页、
而不是显示黑图的原因。

## 编译配置

使用 Designer UI 时投屏自动编入：

```ini
CONFIG_REALTEK_BUILD_DASHBOARD_DESIGNER=y   # 会自动选中投屏
CONFIG_REALTEK_APP_NAVI_PROJECTION=y        # 选中 stream 与 qbcode 控件
```

`CONFIG_REALTEK_APP_NAVI_PROJECTION` 不能手动选择：根 `Kconfig` 里由
`REALTEK_BUILD_DASHBOARD_DESIGNER` 触发。关掉 Designer UI 后，投屏相关源码也不再参与
编译（见 `src/hmi_rtk_bt/hmi_ble/private_service/CMakeLists.txt`）。

## 常见问题

| 现象 | 排查点 |
| --- | --- |
| 能看到服务，但收不到通知 | `0xFFC2` 的 CCCD 没有打开 |
| `0x05 / 0x0B` 没有应答 | 设备固件早于本次握手——沿用本地默认值，`0x10` 照常可用 |
| `0x2B` 解析失败或区域为 0 | 设备上报 0 × 0；保留本地默认值，不要按 0 尺寸编码 |
| BEGIN 回 `0x01`（忙） | 上一个文件会话没关，等 END_RSP 或发 ABORT |
| BEGIN 回 `0x02`（空间不足） | `total_size` 超过 stream transport 缓冲（128 KB） |
| 设备回 `ABORT (reason = 0x01)` | DATA 序号跳变，或写入超出 `total_size` |
| END 回 `0x02`（不完整） | 实际收到的字节少于 `total_size` |
| END 回 `0x01`（CRC） | CRC32 不匹配——确认多项式 `0xEDB88320` 且覆盖整个文件 |
| END 成功但屏幕仍是二维码 | 解码失败（尺寸或堆），查日志 `navi_file: END rejected geometry` |
| 连上后帧立刻被丢 | `chunk_size` 超过 MTU 允许值，改用 BEGIN_RSP 回填的值 |

日志前缀为 `Navi` / `navi_file`（`navi_projection_service.c` 中的 `APP_PRINT_*`），常见几条：
`Navi OPEN rejected`、`navi_file: BEGIN rejected`、`navi_file: DATA out of order`、
`navi_file: END rejected geometry`。

## 维护者提示

- 早期的帧槽传输路径（`navi_handle_frame_packet`、`NAVI_FRAME_SLOTS`、credit、`REPORT`）
  仍在 `navi_projection_service.c` 里，但已经不可达：所有 `0x10` 帧都走文件传输路径。
  不要据此写文档或做对接。
- `0x11` 已删除：投屏会话不再需要 OPEN/CLOSE，几何由 `0x05/0x0B` 上报，且每帧自带 JPEG 头。
  旧的 `navi_open_params_t` 与帧槽相关代码仍在文件里，但已无人调用。
- L1 以前有两份实现（这里和 `src/protocol`），现在统一到 `hmi_proto.c`，CRC 为只覆盖负载的
  CRC-16/ARC；投屏只通过 `proto_l2_build()` 和注册的 sink 收发。帧路径上没有任务、没有队列、
  没有堆分配，流式性能与改造前一致。
- `hmi_l2.c` 里的通用命令表（OTA、设置、绑定、通知、运动、产测、控制、日志）仍没有业务逻辑：
  只记日志、不回包，手机端会超时而不是收到假成功。要支持某条命令，应在这张表里加 handler，
  而不是塞回投屏引擎。
