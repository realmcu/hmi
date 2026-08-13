# 智能手环蓝牙私有通信协议

> 原文档：百度智能手环蓝牙私有通信协议（2014年8月）  
> 模块负责人：陈喜雄 · 项目负责人：孙鹤飞  
> 版权所有：百度在线网络技术（北京）有限公司

---

## 目录

1. [名词解释与约定](#1-名词解释与约定)
2. [协议结构介绍](#2-协议结构介绍)
3. [L2 Command 详解](#3-l2-command-详解)
4. [附录：关键数据结构速查](#附录关键数据结构速查)

---

## 1 名词解释与约定

### 1.1 名词解释

| 术语 | 说明 |
|------|------|
| **设备** | 手环、手表、电子秤等，一般是蓝牙的 Master 端 |
| **手机** | 支持 BLE 的智能手机，也可能是 Bluetooth USB Dongle |

### 1.2 约定

#### 1.2.1 字节序

**本协议所有多字节字段均使用 Big-Endian（大端模式）。**

示例：`uint16_t a = 0xABCD`，传输字节流依次为：`AB CD`。

若一个字节中包含两个值（a=0xa 占低 4 bit，b=0xb 占高 4 bit），字节流内容（二进制）为：`1011 1010`。

#### 1.2.2 L2 层 V-length 注意项

当某 command 下某 key 的 value 为空时，v-length = 0。  
Key Header 固定占用 **2 个字节**，v-length = 0 时这 2 个字节仍须发送，不可省略。

---

## 2 协议结构介绍

### 2.1 协议栈结构

```
┌─────────────────────────────┐
│   Application layer (L2)    │
├─────────────────────────────┤
│   Transport Layer (L1)      │
├─────────────────────────────┤
│   UART Profile (L0)         │
├─────────────────────────────┤
│   BLE Stack                 │
└─────────────────────────────┘
```

---

### 2.2 L0 — UART Profile（BLE）

#### GATT 服务 UUID

| 角色 | UUID |
|------|------|
| UART Profile Service | `6e400001-b5a3-f393-e0a9-e50e24dcca9e` |
| Write Characteristic | `6e400002-b5a3-f393-e0a9-e50e24dcca9e` |
| Read Characteristic  | `6e400003-b5a3-f393-e0a9-e50e24dcca9e`（Notification 模式）|

#### 说明

- **Device 端（Master）**：实现 UART Profile，提供 write + read 两个 characteristic
- **Phone 端（Slave）**：基于 character operation 实现 write interface 和 receive interface
- Phone 端 receive 基于 **notify** 模式；Device 端 receive 基于 **write character**（push 方式，非 pull）
- MTU = **20 bytes**

---

### 2.3 L1 — Transport Layer

#### 功能

- 在 L0 之上实现可靠传输（发送 + 接收）
- L0 MTU = 20 bytes，L1 通过拆包/组包支持最大 **65535 bytes** payload（16-bit 长度字段）
- 实现 **ACK 机制**：接收方校验后发送 success/error ACK
- 重传：收到 error ACK 或超时后重发，失败结果上报 L2

#### L1 数据包结构

```
┌───────────────────────────────────────────────┐
│         L1 Packet (8 ~ 65543 bytes)           │
├──────────────────────────┬────────────────────┤
│   L1 Header (8 bytes)    │  L1 Payload        │
│                          │  (0 ~ 65535 bytes) │
└──────────────────────────┴────────────────────┘
```

#### L1 Header（8 bytes，Big-Endian）

| Byte | 位域 | 宽度 | 说明 |
|------|------|------|------|
| 0 | Magic | 8 bits | 固定值 `0xAB` |
| 1 [7:6] | Reserve | 2 bits | 保留 |
| 1 [5] | ERR flag | 1 bit | 1 = 传输出错 |
| 1 [4] | ACK flag | 1 bit | 1 = 这是 ACK 包 |
| 1 [3:0] | Version | 4 bits | 当前版本 = 0 |
| 2~3 | Payload length | 16 bits | Big-Endian，高字节在前 |
| 4~5 | CRC16 | 16 bits | **CRC-16/ARC**，仅覆盖 Payload（不含 Header），Big-Endian |
| 6~7 | Sequence ID | 16 bits | 包序号，Big-Endian |

#### ACK 包格式

| 包类型 | ACK flag (bit4) | ERR flag (bit5) | Payload length |
|--------|-----------------|-----------------|----------------|
| 数据包 | 0 | 0 | 实际 payload 长度 |
| Success ACK | 1 | 0 | 0 |
| Error ACK | 1 | 1 | 0 |

> ACK 包的 Sequence ID = 被 ACK 的数据包 Sequence ID。

#### L1 版本号：**0**

---

### 2.4 L2 — Application Layer

#### L2 数据包结构

```
┌───────────────────┬───────────────────────────┐
│  L2 Header (2B)   │  L2 Payload (0~65533 B)   │
└───────────────────┴───────────────────────────┘
```

> L2 Payload 上限受 L1 payload 容量约束（最大 65535 B），实际可用大小取决于设备固件缓冲区。

#### L2 Header（16 bits，Big-Endian）

| 位域 | 宽度 | 说明 |
|------|------|------|
| Command ID | 8 bits | 命令类型，见 Command 列表 |
| Version | 4 bits | L2 版本号 |
| Reserve | 4 bits | 保留 |

#### L2 Payload 结构

```
┌─────┬────────────┬───────────┬─────┬────────────┬───────────┬─────┐
│ Key │ Key Header │ Key Value │ Key │ Key Header │ Key Value │ ... │
│ 1B  │   2 bytes  │  N bytes  │ 1B  │   2 bytes  │  N bytes  │     │
└─────┴────────────┴───────────┴─────┴────────────┴───────────┴─────┘
```

#### Key Header（16 bits）

| 位域 | 宽度 | 说明 |
|------|------|------|
| Key Value Length (v-length) | 16 bits | 该 key 的 value 字节数，最大 65535 |

> 原设计 [15:9] 为 Reserve、[8:0] 为 v-length（9 bits，最大 511 bytes）。  
> 已扩展为全 16 bits v-length 以支持大块传输（≥512 bytes）。双端实现须同步更新。

---

## 3 L2 Command 详解

### 3.1 Command 列表

| Command ID | 命令 |
|-----------|------|
| `0x01` | 固件升级命令 |
| `0x02` | 设置命令 |
| `0x03` | 绑定命令 |
| `0x04` | 提醒命令 |
| `0x05` | 运动数据命令 |
| `0x06` | 工厂测试命令 |
| `0x07` | 控制命令 |
| `0x08` | Dump Stack 命令 |
| `0x09` | 测试 Flash 读取命令 |
| `0x0a` | 日志命令 |
| `0x0b` | 文件传输命令 |
| `0x0c` | BLE 连接参数命令 |
| `0x0d` | WiFi 配网命令 |

---

### 3.2 固件升级命令 (command id 0x01)

L2 版本号：**0**

| Key | 定义 |
|-----|------|
| `0x01` | 进入固件升级模式请求 |
| `0x02` | 进入固件升级模式返回 |

#### 0x01 — 进入固件升级模式请求

**方向**：手机 → 设备 | **Value**：空（v-length = 0）

#### 0x02 — 进入固件升级模式返回

**方向**：设备 → 手机 | **Value（2 bytes）**：

| 字段 | 大小 | 说明 |
|------|------|------|
| Status code | 1 byte | `0x00`=OTA 成功；`0x01`=OTA 失败 |
| Error code | 1 byte | `0x01`=电量过低（仅 status=0x01 时有效）|

---

### 3.3 设置命令 (command id 0x02)

L2 版本号：**0**

| Key | 定义 |
|-----|------|
| `0x01` | 时间设置 |
| `0x02` | 闹钟设置 |
| `0x03` | 获取设备闹钟列表请求 |
| `0x04` | 获取设备闹钟列表返回 |
| `0x05` | 计步目标设定 |
| `0x10` | 用户 profile 设置 |
| `0x20` | 防丢设置 |
| `0x21` | 久坐提醒设置 |
| `0x22` | 左右手佩戴设置 |
| `0x23` | 手机操作系统设置 |
| `0x24` | 来电通知电话列表设置 |
| `0x25` | 来电通知开关设置 |

#### 0x01 — 时间设置

**方向**：手机 → 设备（每次绑定成功后需同步）| **Value（32 bits）**：

| 字段 | 宽度 | 有效值 |
|------|------|--------|
| Year | 6 bits | 0~63（从 2000 年起，13=2013）|
| Month | 4 bits | 1~12 |
| Day | 5 bits | 1~31 |
| Hour | 5 bits | 0~23 |
| Minute | 6 bits | 0~59 |
| Second | 6 bits | 0~59 |

#### 0x02 / 0x04 — 闹钟设置 / 获取闹钟列表返回

最多支持 **8 个闹钟**。**Value（5×N bytes）**，每个 Alarm item（40 bits）：

| 字段 | 宽度 | 说明 |
|------|------|------|
| Year | 6 bits | 从 2000 年起 |
| Month | 4 bits | 1~12 |
| Day | 5 bits | 1~31 |
| Hour | 5 bits | 0~23 |
| Minute | 6 bits | 0~59 |
| Id | 3 bits | 闹钟编号（0~7）|
| Reserve | 4 bits | — |
| Day flags | 7 bits | 低位→高位对应周一→周日；1=重复，0=不重复；全 0=只当天有效 |

#### 0x03 — 获取设备闹钟列表请求

**Value**：空

#### 0x05 — 计步目标设定

**Value（4 bytes，Big-Endian）**：  
`Target = Byte1<<24 | Byte2<<16 | Byte3<<8 | Byte4`

#### 0x10 — 用户 Profile 设置

**Value（32 bits）**：

| 字段 | 宽度 | 说明 |
|------|------|------|
| 性别 | 1 bit | 0=女；1=男 |
| 年龄 | 7 bits | 0~127 |
| 身高 | 9 bits | 0.0~256.0 cm（精度 0.5 cm）|
| 体重 | 10 bits | 0.0~512.0 kg（精度 0.5 kg）|
| Reserved | 5 bits | — |

#### 0x20 — 防丢设置

**Value（8 bits）**：低 4 bits = Mode：`0x0`=no alert；`0x1`=middle alert；`0x2`=high alert

#### 0x21 — 久坐提醒设置

**Value（8 bytes）**：

| 字段 | 大小 | 说明 |
|------|------|------|
| Reserved | 1 byte | — |
| 使能开关 | 1 byte | `0x00`=关闭；`0x01`=打开 |
| 阈值 | 2 bytes | 步数阈值（0~65535）|
| 久坐时间 | 1 byte | 分钟，超时才提醒 |
| 开始提醒时间 | 1 byte | 小时（0~23）|
| 结束提醒时间 | 1 byte | 小时（0~23）|
| Day flags | 1 byte | 低→高位对应周一→周日；全 0=只当天 |

#### 0x22 — 左右手佩戴设置

**Value（1 byte）**：`0x01`=左手；`0x02`=右手

#### 0x23 — 手机操作系统设置

**Value（2 bytes）**：Byte0：`0x01`=iOS；`0x02`=Android；Byte1：Reserved

#### 0x24 — 来电通知电话列表设置

**Value（1+20×N bytes，N≤3）**：操作类型（1 byte）+名字或电话号码（20 bytes×N）  
操作类型：`0x01`=增加；`0x02`=替换

#### 0x25 — 来电通知开关

**Value（1 byte）**：`0x01`=使能；`0x02`=关闭

---

### 3.4 绑定命令 (command id 0x03)

L2 版本号：**0**

| Key | 定义 |
|-----|------|
| `0x01` | 绑定用户请求 |
| `0x02` | 绑定用户返回 |
| `0x03` | 用户登录请求 |
| `0x04` | 用户登录返回 |
| `0x05` | 用户解除绑定 |
| `0x06` | 超级绑定 |
| `0x07` | 超级绑定返回 |

#### 0x01 — 绑定用户请求

**Value**：32 bytes 用户 ID（靠低字节对齐，不足用 `0x00` 补齐）

#### 0x02 — 绑定用户返回

**Value（1 byte）**：`0x00`=成功；`0x01`=超时失败

#### 0x03 — 用户登录请求

**Value**：32 bytes 用户 ID（同上）

#### 0x04 — 用户登录返回

**Value（1 byte）**：`0x00`=成功；`0x01`=ID 不一致失败

#### 0x05 — 用户解除绑定

**Value**：1 byte 预留

#### 0x06 — 超级绑定

**Value（64 bytes）**：User ID（32 bytes）+ Super key（32 bytes）

#### 0x07 — 超级绑定返回

**Value（1 byte）**：`0x00`=成功；`0x01`=超时；`0x02`=Super key 校验失败；`0x03`=电量低

---

### 3.5 提醒命令 (command id 0x04)

L2 版本号：**0**

| Key | 定义 | 方向 | Value |
|-----|------|------|-------|
| `0x01` | 来电提醒 | 手机→设备 | 空 |
| `0x02` | 来电已接听 | 手机→设备 | 空 |
| `0x03` | 来电已拒接 | 手机→设备 | 空 |

---

### 3.6 运动数据命令 (command id 0x05)

L2 版本号：**1**

版本 1 以设备持久化记录为数据源。运动记录和睡眠记录分别存放在独立的
FlashDB TSDB 中，线上记录与对应 FlashDB 记录字段一一对应。多字节整数
按本协议统一使用大端序编码，禁止直接复制 MCU 内存中的结构体字节。

版本 1 定义运动/平均心率和睡眠数据的独立分页同步。未在下表标记为
“已定义”的 Key 均为保留编号，手机和设备不得发送，也不得依赖旧版草案
中的 Value 格式。

| Key | 方向 | 状态 | 定义 |
|-----|------|------|------|
| `0x01` | 手机→设备 | 已定义 | 按 Data type 请求当前同步会话的下一页历史数据 |
| `0x02` | 设备→手机 | 已定义 | 返回一页健康历史记录（运动数据 + 平均心率） |
| `0x03` | 设备→手机 | 已定义 | 返回一页睡眠状态变化记录 |
| `0x04` | 设备→手机 | 已定义 | More flag，还有下一页 |
| `0x05` | — | 保留 | 睡眠设置数据 |
| `0x06` | — | 保留 | 实时同步设置 |
| `0x07` | 设备→手机 | 已定义 | 历史同步开始 |
| `0x08` | 设备→手机 | 已定义 | 历史同步结束 |
| `0x09` | — | 保留 | 当天运动汇总 |
| `0x0a` | — | 保留 | 最近一条运动记录 |
| `0x0b` | — | 保留 | 当天运动数据校准 |
| `0x0c` | — | 保留 | 当天运动数据校准返回 |
| `0x0d` | — | 保留 | 独立心率数据 |

#### 版本与通用约束

- SPORT version 1 的 L2 Header 固定为 `05 10`：Command ID=`0x05`，
  Version=`1`，Reserve=`0`。
- 每个 Key 都放在独立的 L2 数据包中；本命令不在同一个 L2 包内合并多个
  Key。
- L2 Header 的 Reserve 必须为 `0`；Health record 的 Flags 未定义位必须为
  `0`。Health record 的 `Reserved` 字段按记录原值传输，接收方当前只保存、
  不解释。
- 手机必须等待 `0x04` More flag 后再请求下一页；不得并发发送多个
  `0x01`。
- 一次 BLE 连接中同一时刻最多存在一个历史同步会话。运动和睡眠必须
  分别发起同步，不得在同一会话中切换 Data type。
- BLE 断开后当前同步会话作废；重新连接后从第一条历史记录重新开始。
- Version 0 的 SPORT 数据格式已废弃，不属于本规范的联调范围。

#### 0x01 — 请求数据

**方向**：手机→设备  
**Value**：Data type（1 byte），v-length 必须为 `1`

| Data type | 数据集 | 返回 Key |
|-----------|--------|----------|
| `0x01` | 运动数据 + 平均心率 | `0x02` |
| `0x02` | 睡眠状态变化记录 | `0x03` |

其他值保留，设备收到后不得创建同步会话。

请求运动数据的完整 L2 字节流：

```text
05 10 01 00 01 01
│  │  │  └───┘  └─ Data type = 0x01（运动）
│  │  │   v-length = 1
│  │  └────── Key = 0x01
│  └───────── Version = 1, Reserve = 0
└──────────── Command ID = 0x05
```

请求睡眠数据：`05 10 01 00 01 02`。

第一次请求创建对应 Data type 的同步会话，并从该 TSDB 最早的可用记录
开始读取。收到 `0x04` 后，下一次请求必须携带相同的 Data type，继续读取
下一页。同步结束后，手机可再发起另一个 Data type 的同步。

#### 0x02 — 健康历史数据返回（运动数据 + 平均心率）

**方向**：设备→手机  
**Value**：Record count（1 byte）+ N × Health record（18 bytes）

每条 `Health record` 同时携带一个统计时间桶内的运动数据和平均心率。
平均心率是否有效由 `Flags.HAS_HR` 指示；它不是独立的实时心率采样。

| 字段 | 宽度 | 约束 |
|------|------|------|
| Record count | 1 byte | `1~8`，必须与后续记录数量一致 |
| Health records | N × 18 bytes | 按 Timestamp 从旧到新排列 |

Value 长度必须满足：`v-length = 1 + Record count × 18`。设备每页最多返回
8 条记录。满页 Value 为 `145 bytes`，完整 L2 包为 `150 bytes`，加上
8-byte L1 Header 后完整 L1 帧为 `158 bytes`；L0 根据实际 ATT payload
大小继续分片。

**Health record（18 bytes）**：

| 偏移 | 字段 | 宽度 | 字节序 | 说明 |
|------|------|------|--------|------|
| 0 | Timestamp | 4 bytes | 大端 | Unix 时间戳（秒），与 FlashDB 记录时间一致 |
| 4 | Steps | 2 bytes | 大端 | 该记录的步数 |
| 6 | Distance | 2 bytes | 大端 | 距离，单位 m |
| 8 | Calories | 2 bytes | 大端 | 卡路里，单位 0.1 kcal |
| 10 | Heart rate | 1 byte | — | 该统计时间桶的平均心率，单位 bpm；仅当 `Flags.HAS_HR=1` 时有效，否则必须为 `0` |
| 11 | Bucket minutes | 1 byte | — | 该记录覆盖的统计时长，单位分钟 |
| 12 | Mode | 1 byte | — | `0`=步行，`1`=跑步，`2`=无效 |
| 13 | Flags | 1 byte | — | bit0=`HAS_HR`，bit1=`PARTIAL_BUCKET`，其余保留 |
| 14 | Reserved | 4 bytes | 大端 | 保留字段，当前发送 FlashDB 中保存的原值 |

`Timestamp` 直接使用 FlashDB 记录的时间，不转换成 `Date + Offset`，也不
根据 `Bucket minutes` 前移或后移时间。手机应以 `(Timestamp, Flags)` 作为
记录语义，不应自行推导或修正记录时间。

单条记录示例：

```text
Timestamp       = 0x12345678
Steps           = 0x1122
Distance        = 0x3344
Calories        = 0x5566
Heart rate      = 0x77
Bucket minutes  = 0x0F
Mode            = 0x01
Flags           = 0x03
Reserved        = 0x00000000
```

对应的完整 L2 字节流：

```text
05 10 02 00 13 01 12 34 56 78 11 22 33 44 55 66 77 0F 01 03 00 00 00 00
│  │  │  └───┘  │  └──────────────────────────────────────────────────┘
│  │  │   len=19 count=1                Health record (18 bytes)
│  │  └──────── Key = 0x02
│  └─────────── Version = 1
└────────────── Command ID = 0x05
```

#### 0x03 — 睡眠状态变化记录返回

**方向**：设备→手机  
**Value**：Record count（1 byte）+ N × Sleep record（8 bytes）

睡眠使用独立的 FlashDB Sleep TSDB。睡眠算法每次确认状态变化时，以该状态
实际生效的 Unix 时间戳写入一条 `sleep_state_record_t`。算法产生的回溯
分钟数必须在写入 FlashDB 前折算到 Timestamp，协议层不再转换时间。

| 字段 | 宽度 | 约束 |
|------|------|------|
| Record count | 1 byte | `1~18`，必须与后续记录数量一致 |
| Sleep records | N × 8 bytes | 按 Timestamp 从旧到新排列 |

Value 长度必须满足：`v-length = 1 + Record count × 8`。设备每页最多返回
18 条记录。满页 Value 为 `145 bytes`，完整 L2 包为 `150 bytes`，完整
L1 帧为 `158 bytes`。

**Sleep record（8 bytes）**：

| 偏移 | 字段 | 宽度 | 字节序 | 说明 |
|------|------|------|--------|------|
| 0 | Timestamp | 4 bytes | 大端 | 状态实际生效的 Unix 时间戳（秒），与 FlashDB 记录时间一致 |
| 4 | State | 1 byte | — | 睡眠状态，见下表 |
| 5 | Flags | 1 byte | — | bit0=`BACKFILLED`，其余位保留且必须为 `0` |
| 6 | Reserved | 2 bytes | 大端 | 保留，发送记录中保存的原值；接收方当前只保存、不解释 |

**State**：

| 值 | 状态 | 说明 |
|----|------|------|
| `0x00` | OFF | 设备未佩戴 |
| `0x01` | DEEP | 深睡 |
| `0x02` | LIGHT | 浅睡 |
| `0x03` | WAKE | 清醒 |
| `0x04` | REM | 快速眼动睡眠 |

`BACKFILLED=1` 表示算法在确认状态后，将状态生效时间回溯到了回调时刻
之前。无论该标志是否置位，Timestamp 都已经是最终生效时间，手机不得
再次减去确认延迟。

每条记录表示“从 Timestamp 开始进入 State”。该状态持续到下一条 Sleep
record 的 Timestamp；最后一条记录持续到后续同步收到的新状态记录。
记录可跨越午夜，手机不得按自然日拆分或修正 Timestamp。

单条睡眠记录示例：

```text
Timestamp = 0x12345678
State     = 0x01（DEEP）
Flags     = 0x01（BACKFILLED）
Reserved  = 0x0000
```

对应的完整 L2 字节流：

```text
05 10 03 00 09 01 12 34 56 78 01 01 00 00
│  │  │  └───┘  │  └────────────────────┘
│  │  │   len=9 count=1       Sleep record (8 bytes)
│  │  └──────── Key = 0x03
│  └─────────── Version = 1
└────────────── Command ID = 0x05
```

#### 0x04 — More flag

**方向**：设备→手机  
**Value**：Data type（1 byte），v-length 必须为 `1`

- 运动数据还有更多：`05 10 04 00 01 01`
- 睡眠数据还有更多：`05 10 04 00 01 02`

设备发送本 Key 表示当前页之后仍有记录。手机收到后应发送一个新的
`0x01`，并携带相同的 Data type 请求下一页。没有更多记录时设备不得
发送本 Key。

#### 0x07 / 0x08 — 历史数据同步开始 / 结束

**方向**：设备→手机  
**Value**：Data type（1 byte），v-length 必须为 `1`

- 运动同步开始：`05 10 07 00 01 01`
- 运动同步结束：`05 10 08 00 01 01`
- 睡眠同步开始：`05 10 07 00 01 02`
- 睡眠同步结束：`05 10 08 00 01 02`

`0x07` 在一次同步会话中只发送一次，且必须先于任何 `0x02`。`0x08`
表示会话正常结束；手机收到后不得继续用旧会话请求下一页。Start、More、
End 的 Data type 必须与该会话请求和数据返回类型一致。睡眠同步时 `0x07`
必须先于任何 `0x03`。

#### 历史同步时序

有多页数据：

```text
手机                                  设备
 │  0x01 请求第一页                    │
 │ ─────────────────────────────────> │
 │                 0x07 同步开始       │
 │ <───────────────────────────────── │
 │                 0x02 第一页记录     │
 │ <───────────────────────────────── │
 │                 0x04 还有更多       │
 │ <───────────────────────────────── │
 │  0x01 请求下一页                    │
 │ ─────────────────────────────────> │
 │                 0x02 下一页记录     │
 │ <───────────────────────────────── │
 │                 0x08 同步结束       │
 │ <───────────────────────────────── │
```

没有历史数据：

```text
手机                                  设备
 │  0x01 请求第一页                    │
 │ ─────────────────────────────────> │
 │                 0x07 同步开始       │
 │ <───────────────────────────────── │
 │                 0x08 同步结束       │
 │ <───────────────────────────────── │
```

空库时不发送 Record count=`0` 的 `0x02` 数据包。

#### 异常与兼容处理

- 收到未知或保留 Key：接收方忽略该 Key，不改变当前同步会话。
- 收到 Value 长度与定义不符的 Key：接收方丢弃该 Key。
- `0x02` 的 Record count 超出 `1~8`，或 v-length 不满足
  `1 + count × 18`：手机必须丢弃整页，不得入库。
- Timestamp 相同的记录仍按接收顺序处理；手机不得仅以 Timestamp 去重。
- L1 发送失败、BLE 断开或设备读取存储失败时，本次同步会话终止；手机
  重新连接或重新发起同步时必须允许从头同步并自行去重。

---

### 3.7 工厂测试命令 (command id 0x06)

L2 版本号：**0**

| Key | 定义 |
|-----|------|
| `0x01` | 请求 echo 服务 |
| `0x02` | Echo 服务返回 |
| `0x03` | 请求 charge 信息 |
| `0x04` | 返回 charge 信息 |
| `0x05` | 点亮 LED |
| `0x06` | 震动马达 |
| `0x07` | 写 SN |
| `0x08` | 读 SN |
| `0x09` | SN 返回 |
| `0x0a` | 写 test flag |
| `0x0b` | 读 test flag |
| `0x0c` | test flag 返回 |
| `0x0d` | 请求 sensor 数据 |
| `0x0e` | 返回 sensor 数据 |
| `0x10` | 进入测试模式（超级命令）|
| `0x11` | 退出测试模式（超级命令）|
| `0x21` | 按键测试 |
| `0x31` | 马达老化测试 |
| `0x32` | LED 老化测试 |

#### 0x01/0x02 — Echo

请求 Value：N bytes 字符串；返回 Value：原封不动返回

#### 0x03/0x04 — Charge 信息

请求 Value：空；返回 **Value（2 bytes）**：Voltage（16 bits，负数表示未检测到）

#### 0x05 — 点亮 LED

**Value**：空=点亮所有 LED；`0x00`=第一行；`0x01`=第二行；...

#### 0x06 — 震动马达

**Value**：空

#### 0x07/0x08/0x09 — SN 写/读/返回

写请求/返回 Value：32 bytes SN；读请求 Value：空

#### 0x0a/0x0b/0x0c — test flag 写/读/返回

写请求/返回 Value：1 byte flag；读请求 Value：空

#### 0x0d/0x0e — sensor 数据请求/返回

请求 Value：空；返回 **Value（48 bits）**：X axis（16b）+ Y axis（16b）+ Z axis（16b）

#### 0x10/0x11 — 进入/退出测试模式

**Value**：32 bytes token（设备验证通过后才执行）

#### 0x21 — 按键测试

**方向**：设备 → 手机 | **Value（8 bytes）**：

| 字段 | 大小 | 说明 |
|------|------|------|
| Code | 1 byte | `0x00`=DOWN；`0x01`=UP；`0x02`=SHORT_CLICK；`0x03`=LONG_CLICK |
| Id | 1 byte | Button ID |
| Reserved | 2 bytes | — |
| Timestamp | 4 bytes | 从 2000 年起的秒数 |

#### 0x31/0x32 — 马达/LED 老化测试

**Value（1 byte）**：`0x01`=开始；`0x02`=结束

---

### 3.8 控制命令 (command id 0x07)

L2 版本号：**0**

| Key | 定义 | 方向 | Value |
|-----|------|------|-------|
| `0x01` | 拍照控制 | 设备→手机 | 空 |
| `0x02` | 单击控制 | 设备→手机 | 空 |
| `0x03` | 双击控制 | 设备→手机 | 空 |
| `0x11` | 相机应用状态请求 | 手机→设备 | `0x00`=前台；`0x01`=后台 |

---

### 3.9 Dump Stack 命令 (command id 0x08)

> 暂未实现

| Key | 定义 |
|-----|------|
| `0x01` | 手机请求获取手环 assert 位置信息 |
| `0x02` | 手环返回 assert 位置信息 |
| `0x03` | 手机请求获取 assert 栈信息 |
| `0x04` | 手环反馈 assert 栈信息 |

---

### 3.10 测试 Flash 读取命令 (command id 0x09)

> 暂未实现

---

### 3.11 日志命令 (command id 0x0a)

L2 版本号：**0**

| Key | 定义 | 方向 | Value |
|-----|------|------|-------|
| `0x01` | 打开日志功能 | 手机→设备 | 空 |
| `0x02` | 关闭日志功能 | 手机→设备 | 空 |
| `0x03` | 日志发送 | 设备→手机 | 0~499 bytes 日志字符串 |

---

### 3.12 文件传输命令 (command id 0x0b)

L2 版本号：**0**

#### 概述

文件传输命令在 L1 可靠传输之上提供一套应用层会话机制，支持图片、视频及原始二进制数据从手机到设备的单向传输。传输流程如下：

```
手机                                    设备
 │                                        │
 │──── XFER_BEGIN_REQ (0x01) ────────────▶│  协商文件类型、大小、块大小
 │◀─── XFER_BEGIN_RSP (0x02) ────────────│  接受或拒绝；返回实际块大小
 │                                        │
 │──── XFER_DATA      (0x03) [seq=0] ───▶│  ┐
 │──── XFER_DATA      (0x03) [seq=1] ───▶│  │ 连续发送，L1 可靠传输保证到达
 │──── XFER_DATA      (0x03) [seq=2] ───▶│  ┘ 序号错误时设备发 XFER_ABORT
 │              …                         │
 │──── XFER_END_REQ   (0x05) ───────────▶│  携带完整文件的 CRC32
 │◀─── XFER_END_RSP   (0x06) ───────────│  校验结果
 │                                        │
```

任意一方均可在会话期间发送 XFER_ABORT（0x07）立即终止传输。同一时刻仅允许一个活跃传输会话；设备在会话未结束时收到新的 XFER_BEGIN_REQ，应回复 status = `0x01`（设备忙）。

#### Key 列表

| Key | 定义 | 方向 |
|-----|------|------|
| `0x01` | 传输会话建立请求 | 手机 → 设备 |
| `0x02` | 传输会话建立响应 | 设备 → 手机 |
| `0x03` | 数据块发送 | 手机 → 设备 |
| `0x05` | 传输结束请求 | 手机 → 设备 |
| `0x06` | 传输结束响应 | 设备 → 手机 |
| `0x07` | 传输中止 | 双向 |

---

#### 0x01 — 传输会话建立请求（XFER_BEGIN_REQ）

**方向**：手机 → 设备 | **Value（8 + filename_len bytes）**：

| 字段 | 大小 | 说明 |
|------|------|------|
| file_type | 1 byte | `0x01`=图片；`0x02`=视频；`0x03`=原始数据 |
| total_size | 4 bytes | 文件总字节数（Big-Endian）|
| chunk_size | 2 bytes | 请求的单块字节数，有效范围 1~2048（Big-Endian）|
| filename_len | 1 byte | 文件名字节数；`0x00` 表示不携带文件名 |
| filename | filename_len bytes | UTF-8 编码文件名，不含 null 终止符 |

> `chunk_size` 为手机期望值，设备可在响应中调小；双方以 XFER_BEGIN_RSP 中的值为准。

---

#### 0x02 — 传输会话建立响应（XFER_BEGIN_RSP）

**方向**：设备 → 手机 | **Value（3 bytes）**：

| 字段 | 大小 | 说明 |
|------|------|------|
| status | 1 byte | `0x00`=接受；`0x01`=设备忙；`0x02`=存储空间不足；`0x03`=不支持的文件类型 |
| chunk_size | 2 bytes | 设备实际接受的块大小（仅 status=`0x00` 时有效；Big-Endian）|

---

#### 0x03 — 数据块发送（XFER_DATA）

**方向**：手机 → 设备 | **Value（2 + data_len bytes）**：

| 字段 | 大小 | 说明 |
|------|------|------|
| seq | 2 bytes | 块序号，从 `0` 开始单调递增（Big-Endian）|
| data | 1~chunk_size bytes | 块数据；最后一块可小于 chunk_size |

> L1 可靠传输保证数据块按序到达；设备若检测到序号不连续，应发送 XFER_ABORT（reason=`0x01`）。

---

#### 0x05 — 传输结束请求（XFER_END_REQ）

**方向**：手机 → 设备 | **Value（4 bytes）**：

| 字段 | 大小 | 说明 |
|------|------|------|
| crc32 | 4 bytes | 完整文件的 CRC32 校验值（多项式 `0xEDB88320`，Big-Endian）|

---

#### 0x06 — 传输结束响应（XFER_END_RSP）

**方向**：设备 → 手机 | **Value（2 bytes）**：

| 字段 | 大小 | 说明 |
|------|------|------|
| status | 1 byte | `0x00`=成功；`0x01`=CRC32 校验失败；`0x02`=数据不完整 |
| error_code | 1 byte | `0x00`=无；`0x01`=写入失败；`0x02`=解码失败（仅 status≠`0x00` 时有效）|

---

#### 0x07 — 传输中止（XFER_ABORT）

**方向**：双向 | **Value（1 byte）**：

| 字段 | 大小 | 说明 |
|------|------|------|
| reason | 1 byte | `0x00`=用户取消；`0x01`=传输错误；`0x02`=超时 |

> 收到 XFER_ABORT 的一方应立即停止传输并释放会话资源，无需回复。

---

### 3.13 BLE 连接参数命令 (command id 0x0c)

L2 版本号：**0**

| Key | 定义 |
|-----|------|
| `0x01` | 请求 BLE 连接参数（手机→设备）|
| `0x02` | 返回 BLE 连接参数（设备→手机）|

#### 0x01 — 请求 BLE 连接参数

**方向**：手机 → 设备 | **Value**：空（v-length = 0）

#### 0x02 — 返回 BLE 连接参数

**方向**：设备 → 手机 | **Value（7 bytes，Big-Endian）**：

| 字段 | 大小 | 说明 |
|------|------|------|
| conn_interval | 2 bytes | 当前连接间隔，单位 1.25ms（例：0x0018 = 24 × 1.25ms = 30ms）|
| conn_latency | 2 bytes | 从设备延迟（Slave Latency）|
| conn_supervision_timeout | 2 bytes | 监督超时，单位 10ms（例：0x01F4 = 500 × 10ms = 5000ms）|
| conn_mtu_size | 1 byte | 当前 ATT MTU 大小（字节）|

---



### 3.14 WiFi 配网命令 (command id 0x0d)

L2 版本号：**0**

#### 概述

WiFi 配网命令允许 App 通过已建立的 BLE 连接将 WiFi 凭据（SSID/密码）下发给设备，监听配网状态，配网成功后获取设备 TCP 服务的 IP 和端口，用于后续切换至 WiFi 传输通道。

#### 配网交互流程

```
App                                     Device
 │                                        │
 │──── WIFI_CONFIG_SET (0x01) ──────────▶│  下发 SSID / 密码
 │◀─── WIFI_CONFIG_ACK (0x02) ──────────│  接受或拒绝（5s 超时）
 │                                        │
 │      （设备在后台发起 WiFi 连接）       │
 │                                        │
 │◀─── WIFI_STATUS     (0x04) ──────────│  主动上报状态变更（connecting / connected / failed）
 │                                        │
 │  [可选：30s 后超时未收到最终状态]       │
 │──── WIFI_STATUS_REQ (0x03) ──────────▶│  主动查询当前状态
 │◀─── WIFI_STATUS     (0x04) ──────────│  返回当前状态
```

- App 收到 `WIFI_CONFIG_ACK` 后启动 30s 状态超时定时器；
- 超时后发送一次 `WIFI_STATUS_REQ` 主动查询，再等 10s；仍无响应则判定配网失败；
- 设备连接成功后在 `WIFI_STATUS` 中携带 IP 和 TCP 端口，App 可随即发起 TCP 连接。

#### Key 列表

| Key | 定义 | 方向 |
|-----|------|------|
| `0x01` | WIFI_CONFIG_SET — 下发 SSID/密码 | App → Device |
| `0x02` | WIFI_CONFIG_ACK — 确认是否接受请求 | Device → App |
| `0x03` | WIFI_STATUS_REQ — 主动查询配网状态 | App → Device |
| `0x04` | WIFI_STATUS — 上报配网状态 / IP | Device → App |

---

#### 0x01 — WIFI_CONFIG_SET

**方向**：App → Device | **Value（5 + ssid_len + pwd_len bytes）**：

| 字段 | 大小 | 说明 |
|------|------|------|
| request_id | 2 bytes | 请求序号（Big-Endian），用于与 ACK 匹配；每次配网递增 |
| flags | 1 byte | 位标志：bit0 = `FLAG_SAVE_CREDENTIALS`（1 = 保存凭据到设备，下次自动连接） |
| ssid_len | 1 byte | SSID UTF-8 编码后字节数（1~32） |
| ssid | ssid_len bytes | SSID，UTF-8 编码，不含 null 终止符 |
| pwd_len | 1 byte | 密码 UTF-8 编码后字节数（0~64）；0 表示开放网络，无密码 |
| password | pwd_len bytes | 密码，UTF-8 编码，不含 null 终止符 |

---

#### 0x02 — WIFI_CONFIG_ACK

**方向**：Device → App | **Value（4 bytes）**：

| 字段 | 大小 | 说明 |
|------|------|------|
| request_id | 2 bytes | 对应的请求序号（Big-Endian） |
| result | 1 byte | `0x00`=接受；`0x01`=拒绝 |
| error | 1 byte | 错误码（仅 result=`0x01` 时有效；接受时置 `0x00`） |

**error 错误码**：

| 错误码 | 说明 |
|--------|------|
| `0x00` | 无错误 |
| `0x01` | Payload 格式错误 |
| `0x02` | 不支持的配网模式 |
| `0x03` | 无效的 SSID |
| `0x04` | 无效的密码 |
| `0x05` | 设备繁忙（已有配网会话进行中）|

---

#### 0x03 — WIFI_STATUS_REQ

**方向**：App → Device | **Value（2 bytes）**：

| 字段 | 大小 | 说明 |
|------|------|------|
| request_id | 2 bytes | 对应的请求序号（Big-Endian）|

> 设备收到此命令后应立即回复当前配网状态（`WIFI_STATUS`），即使状态仍为 idle。

---

#### 0x04 — WIFI_STATUS

**方向**：Device → App | **Value（6 + ip_len bytes）**：

| 字段 | 大小 | 说明 |
|------|------|------|
| request_id | 2 bytes | 对应的请求序号（Big-Endian）|
| state | 1 byte | 配网状态码（见下表）|
| error | 1 byte | 错误码（仅 state=`0x03` 时有效；其余状态置 `0x00`）|
| ip_len | 1 byte | IP 字符串 ASCII 编码字节数；未连接时为 `0x00` |
| ip | ip_len bytes | 点分十进制 IP 地址字符串（如 `"192.168.1.100"`），ASCII 编码，不含 null 终止符 |
| port | 2 bytes | TCP 服务端口号（Big-Endian；默认 8783）|

**state 状态码**：

| 状态码 | 说明 |
|--------|------|
| `0x00` | Idle（空闲，未发起连接）|
| `0x01` | Connecting（正在连接 WiFi）|
| `0x02` | Connected（已连接，ip/port 字段有效）|
| `0x03` | Failed（连接失败，error 字段有效）|

**error 错误码**（state=`0x03` 时）：

| 错误码 | 说明 |
|--------|------|
| `0x00` | 无错误 |
| `0x01` | WiFi 认证失败（SSID/密码错误）|
| `0x02` | 未找到该 WiFi（AP 不在范围内）|
| `0x03` | DHCP 失败 |
| `0x04` | 连接超时 |
| `0x05` | TCP 服务启动失败 |
| `0x06` | 未知错误 |

---

## 附录：关键数据结构速查

### L1 Header 字节布局（Big-Endian）

```
Byte 0:  [7:0]  Magic = 0xAB
Byte 1:  [7:6]  Reserve
         [5]    ERR flag   (1=出错)
         [4]    ACK flag   (1=ACK包)
         [3:0]  Version    (当前=0)
Byte 2:  [7:0]  Payload length 高字节
Byte 3:  [7:0]  Payload length 低字节
Byte 4:  [7:0]  CRC16 高字节
Byte 5:  [7:0]  CRC16 低字节
Byte 6:  [7:0]  Sequence ID 高字节
Byte 7:  [7:0]  Sequence ID 低字节
```

### CRC16 计算范围

算法 **CRC-16/ARC**：多项式 `0x8005`（反射 `0xA001`），初值 `0x0000`，输入/输出均反射（refin=refout=true），无末异或（xorout=0）。**仅覆盖 Payload**，不含 Header。空 payload（ACK 包）的 CRC 为初值 `0x0000`。自检：ASCII `"123456789"` → `0xBB3D`。

### L2 Header 字节布局（Big-Endian）

```
Byte 0:  [7:0]  Command ID
Byte 1:  [7:4]  Version
         [3:0]  Reserve
```

### Date 字段（16 bits，各命令通用）

```
[15]    Reserve
[14:9]  Year   (0~63, 从 2000 年起)
[8:5]   Month  (1~12)
[4:0]   Day    (1~31)
```
