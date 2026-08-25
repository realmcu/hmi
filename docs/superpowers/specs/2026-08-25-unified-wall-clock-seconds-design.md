# 统一时间口径:墙上时钟秒

日期:2026-08-25
状态:设计已确认,待实现
影响面:BLE 协议文档、固件(applications)、手机 App(HoneyBox)

## 1. 背景

固件当前对"一个 `uint32_t` 时间值代表什么"存在两种互相矛盾的解释,
同一个值在不同函数里被当作两种东西使用。

写入侧按本地时间处理:`app_time_set_local()` 的命名、参数类型
`app_time_local_t`、以及 `app_time.h` 的注释 "Store a validated local
calendar value in the hardware RTC",都表明 RTC 存的是本地日历值。
协议 0x01 下发的 32-bit packed 值不含任何时区字段,按手表业务惯例是
用户本地时间。

读出侧却按 UTC 处理:`app_time_now()` 直接返回
`civil_to_epoch(RTC)`,声明为 "Wall-clock time in Unix seconds",
随后 `app_time_to_local()` 和 `local_sec_from_utc()` 又对它施加
`+ s_tz_min * 60`(默认 +480 分钟)。

### 1.1 已确认的现存缺陷

由于 RTC 实际存的是本地时间,那次 `+8h` 是多加的:

1. **`EVT_TIME_DAY_CHANGED` 在本地 16:00 触发,而非午夜。**
   `app_time.c:242-243` 对已是本地口径的值再加 28800 秒,
   `local / 86400` 的翻页点被推后 8 小时。
   `health_worker_on_day_changed()`(`health_worker.c:125-130`)
   因此每天下午四点清零 `s_today_acc`,用户当日步数归零。
   这是用户可见的功能性缺陷。

2. **SPORT 同步日志显示时间偏 +8 小时。**
   `hmi_l2_cmd_sport.c:189` 对已是本地口径的 `rec.ts_utc`
   再施加时区偏移。

3. **`EVT_TIME_TICK_15MIN` 不受影响。** 28800 是 900 的整数倍,
   多平移不改变 `% 900` 的结果。这正是缺陷长期未被发现的原因:
   15 分钟分桶表现正常,只有跨天和显示出问题。

4. **`ts_utc` 字段名与内容不符。** 但因协议要求手机不做时区换算,
   落库与上报的最终行为是自洽正确的 —— 仅命名撒谎。

### 1.2 协议文档的现状

`component/protocol/BLE_PROTOCOL_SPEC.html` 中:

- 0x01 时间设置:Value 32 bits = Year(6) + Month(4) + Day(5) +
  Hour(5) + Minute(6) + Second(6),无时区字段,全文未出现 "UTC"。
- 运动/睡眠记录 Timestamp:标注为"Unix 时间戳(秒),与 FlashDB
  记录时间一致"。
- 全文唯一一次出现"时区":"手机不得把 Timestamp 当作桶开始时间,
  也不得再对 Timestamp 做时区、Date/Offset 或 15 分钟前移转换。"

后两条并读可知,文档意图是手机拿到 Timestamp 直接当墙上时间显示。
既然手机不换算,而手机下发的又是用户本地日历值,则整条链上实际不存在
UTC —— 系统事实上运行的是"本地时间冒充 Unix 秒"的口径,只是命名和
注释没有承认这一点。

## 2. 目标与非目标

**目标**:让"这个 `uint32_t` 是什么口径"不再是一个需要问的问题。
协议、存储、固件内部统一为单一标量,时区不进入数据流。

**非目标**:

- 不做时区协商。固件中不存在时区概念(此前考虑过的
  "协议增加时区协商 key" 已撤销)。
- 不追求跨时区旅行时历史数据在绝对时间轴上的连续性(见 §7 权衡)。
- 不改闹钟表示(见 §4.3)。
- 不改 0x21 按键测试时间戳(见 §4.4)。

## 3. 核心模型

### 3.1 唯一标量

> **墙上时钟秒(wall clock seconds)**:`uint32_t`,自
> **1970-01-01 00:00:00** 起算的秒数,其值为"表盘应当显示的时刻"。
> 锚点由手机单方定义:手机取本地日历字段,按 UTC 折算规则生成秒数。
> **固件收到后永不做任何加减。** 有效范围 1970–2106。

纪元固定为 1970-01-01 00:00:00,即标准 Unix 纪元。**不使用 2000 年
纪元,因此两侧都不需要任何纪元换算代码**(不存在
`± 946684800` 之类的常数)。理由:

1. 两侧库函数原生以 1970 为原点 —— newlib `gmtime_r` 的 `time_t`、
   Dart `DateTime.fromMillisecondsSinceEpoch` 均是。任何自定义纪元
   都要求每次转换手工加减常数,一侧漏加即静默偏 30 年,与本设计要
   消灭的"偏 8 小时"属同类缺陷。
2. `uint32_t` 以 1970 起算可用至 2106-02-07,手表寿命内充裕。
   注意此处与 `int32_t` 的 2038 问题无关:新模型无有符号中间量,
   秒数直存直用,`app_time.c:156-168` 原有的溢出饱和逻辑随
   `local_sec_from_utc()` 一并删除。
3. FlashDB 的 `fdb_time_t` 约定即 Unix 纪元。`health_db.c:103`
   把记录时间戳直接作为 TSDB 时间键使用,换用其他纪元会使时间索引
   与外部认知错开 30 年,并牵连 `fdb_tsl_iter_by_time` 的查询区间。

### 3.2 固件侧的三条推论

全部是纯算术,不含任何偏移量:

| 用途 | 表达式 |
|---|---|
| 跨天检测 | `sec / 86400` |
| 刻钟边界检测 | `sec % 900 == 0` |
| 渲染为日历字段 | `gmtime_r(&sec, &tm)` |

`gmtime_r` 在此是**正确**选择而非将就:秒数由手机按 UTC 规则折算
而来,用同一规则折算回去是严格的逆运算。**不得使用 `localtime_r`
或 `mktime`** —— 二者会读取 `TZ` 环境变量,在嵌入式环境下 `TZ` 未设时
行为依赖 newlib 实现细节,正是本设计要消除的不确定性。

### 3.3 固件中消失的概念

- `s_tz_min`(`app_time.c:60`)—— 删除,时区概念不再存在。
- `local_sec_from_utc()`(`app_time.c:162-169`)—— 删除,不再需要平移。
- `utc` / `local` 这两个词 —— 从所有**表示秒数**的标识符和注释中移除,
  统一为 wall clock seconds。

保留 `app_time_local_t` 类型名(§5.1 中该类型本身不删),
因为它承载的是**日历字段集合**而非秒数,"local" 在此指"人类可读的
本地日历表示",与秒数口径无关。若嫌歧义可更名
`app_time_calendar_t`;本设计不强制,实现时择一并保持一致。

### 3.4 关于使用 C 库替代手写实现

设计意图是尽量交给 libc,但存在一处硬约束:**newlib 未提供
`timegm`**(已核验工具链 `libc.a` 符号表:仅有 `gmtime_r`、
`localtime_r`、`mktime`)。而 `mktime` 依赖 `TZ`,不可用于本设计。

新模型下写路径不再需要"日历 → 秒":手机直接下发秒数,
故 `hmi_l2_decode_time()` 的位解包、`days_in_month()` 校验整块删除
——— 不是替换为 libc 调用,而是**需求消失**。

读路径仍需要"日历 → 秒",因为硬件 RTC 寄存器只存日历字段,
`app_time_now()` 必须把读回的日历值折算成秒。此处**保留现有的
`civil_to_epoch()`**(`app_time.c:117-141`,Howard Hinnant 算法):
它已验证、注释完整、不依赖 `TZ`,而 libc 在此没有不带时区的等价物。
用 `mktime` 替代会重新引入时区依赖,是退步。

`app_time_set_local()` 写 RTC 时需要"秒 → 日历字段",方向为正,
使用 `gmtime_r`。副产品是 `tm_wday` 由 libc 免费提供,
顺带修复 §5.2 记录的 weekday 缺陷。

## 4. 协议变更

对 `component/protocol/BLE_PROTOCOL_SPEC.html` 的修改。
**本次为不兼容(断裂式)变更:固件与 App 必须同版本升级**,
不引入版本门做兼容(见 §7)。

### 4.1 0x01 时间设置 —— 格式变更

Value 由 32-bit packed 日历字段改为 **4 bytes 大端墙上时钟秒**。
字节宽度不变(仍 4 字节),语义完全不同。

文档需明确记载:

- 该秒数自 1970-01-01 00:00:00 起算。
- 其值为"设备应当显示的墙上时钟时刻"。
- 手机负责把用户本地日历折算为秒数;**设备不做任何时区处理**。

原 packed 格式中 Year 字段"从 2000 年起、6 bits(0~63)"是位宽压缩
的产物,不是纪元选择;改为 4 字节秒后该约束消失。

### 4.2 上行 Timestamp —— 仅语义变更

运动(0x02)与睡眠(0x03)记录的 Timestamp 线格式不变
(已是 4 字节大端秒)。文档措辞修改:

- 删除"Unix 时间戳(秒)"这一容易被误读为 UTC 的表述,
  改为"墙上时钟秒(自 1970-01-01 起算,与设备显示一致)"。
- 现有"手机不得再对 Timestamp 做时区转换"一句保留,但改为正面陈述:
  该 Timestamp 已是应显示的时刻,直接渲染即可。此约束在新模型下
  是自然结果,不再是需要解释的例外。

### 4.3 闹钟 0x02/0x04 —— 不变

保持 40-bit 日历字段格式。闹钟携带 Day flags(周重复规则),
表达的是重复规则而非单一时间点,秒数无法表示。
文档中显式标注:这是协议中唯一使用日历字段表示时间的位置。

### 4.4 0x21 按键测试 Timestamp —— 不变

保持"从 2000 年起的秒数"。属工厂测试路径,固件尚未实现。
文档中显式标注为**全协议唯一的非 1970 纪元例外**,
避免后续实现照抄。

## 5. 固件改动

### 5.1 改动清单

| 文件 | 改动 |
|---|---|
| `app/app_time/app_time.h` | `app_time_set_local(const app_time_local_t*)` → `app_time_set(uint32_t sec)`;`app_time_now()` 注释改为墙上时钟秒;`app_time_to_local()` 更名 `app_time_to_calendar()`,职责收窄为纯渲染 |
| `app/app_time/app_time.c` | 删除 `s_tz_min`、`local_sec_from_utc()`;保留 `civil_to_epoch()` 供 `app_time_now()` 读 RTC 使用;`app_time_set()` 用 `gmtime_r` 把秒展开为 `posix_rtc_time_t`;`rtc_second_cb()` 中的天索引与刻钟判断直接用秒值,去掉平移;更新文件头注释,删除 "Local means UTC + s_tz_min" 段落 |
| `app/app_core/app_event_defs.h` | `app_evt_time_synced_t` 由六个日历字段改为单个 `uint32_t sec`;更新注释 |
| `app/app_protocol/hmi_l2_cmd_settings.c` | 删除 `hmi_l2_decode_time()` 的位解包与 `days_in_month()`;改为读 4 字节大端秒;调用 `app_time_set()`;发布 `EVT_TIME_SYNCED` 载荷改为秒 |
| `app/app_health/app_health_internal.h` | `health_pedo_record_t.ts_utc` → `ts`;注释 "UTC epoch seconds captured at flush moment" → 墙上时钟秒 |
| `app/app_health/health_worker.c` | `ts_utc` → `ts`;`boundary_utc` → `boundary_sec` |
| `app/app_health/health_db.c` | `ts_utc` → `ts`(共 10 处);`health_db_save_synced_ts()` 参数改名 |
| `app/app_protocol/hmi_l2_cmd_sport.c` | `sport_encode_record()` 字段改名;`app_time_to_local` 调用改为 `app_time_to_calendar`(此时它确实只做渲染) |

`ts_utc` → `ts` 是纯改名:字段类型、偏移、结构体布局均不变,
**Flash 中已有记录不受影响,无需数据迁移**。

### 5.2 顺带修复:weekday 撒谎

`hmi_l2_cmd_settings.c:54` 当前把 `weekday` 填 0 后写入 RTC,
而 `posix_ioctl_rtc.h` 的约定是"驱动填不了时置 `0xFF`",
0 表示星期日 —— 即当前实现向 RTC 谎报周日。若 Realtek port 将 wday
写入硬件寄存器,或闹钟使用 `POSIX_RTC_ALARM_MASK_WEEKDAY`,会出错。

新实现中 `app_time_set()` 用 `gmtime_r` 展开秒数,`tm_wday` 由 libc
计算,直接填入 `posix_rtc_time_t.wday`,缺陷自然消失。

### 5.3 不变的部分

- `EVT_TIME_TICK_15MIN` / `EVT_TIME_DAY_CHANGED` 的事件 ID 与
  `uint32_t` 载荷类型不变,仅语义澄清为墙上时钟秒。
- RTC 1 Hz 中断驱动边界事件的机制不变。
- ISR 纪律不变:`rtc_second_cb()` 中仍只做 GET_TIME ioctl 与
  `app_event_publish_isr()`。
- FlashDB TSDB 的记录布局与时间键用法不变。

## 6. 手机侧改动(HoneyBox)

### 6.1 `lib/services/watch_time_protocol.dart`

`buildSetTime(DateTime localTime)` 改为发送 4 字节大端秒:

```dart
final seconds = DateTime.utc(
  localTime.year, localTime.month, localTime.day,
  localTime.hour, localTime.minute, localTime.second,
).millisecondsSinceEpoch ~/ 1000;
```

**关键点:必须用 `DateTime.utc(...)` 而非 `DateTime(...)`。**
用后者会二次施加设备时区偏移,得到错误的秒数。
此处取的是 `localTime` 的**日历字段**,以 UTC 规则折算,
这正是 §3.1 定义的锚点生成方式。

删除 packed 位运算。`minimumYear`/`maximumYear` 校验可保留作为
合理性检查,但不再是格式约束。

调用点 `lib/providers/watch_bind_provider.dart:141`
(`WatchTimeProtocol.buildSetTime(_clock())`,`_clock` 默认为
`DateTime.now`)无需修改。

### 6.2 `lib/services/watch_health_protocol.dart`

`_readTimestamp()`(第 301-307 行)当前实现:

```dart
return DateTime.fromMillisecondsSinceEpoch(seconds * 1000, isUtc: true)
    .toLocal();
```

**去掉 `.toLocal()`**,保留 `isUtc: true`。秒数已是应显示的时刻,
再做 `toLocal()` 会二次施加时区偏移。同时更新第 300 行注释
"Records are UTC on the wire"。

### 6.3 `lib/pages/watch/health/watch_health_data.dart` —— 必须一并核对

去掉 `.toLocal()` 后,`record.timestamp` 成为 UTC-flagged 的
`DateTime`。Dart 的 `.hour`/`.year` 等取值器对 UTC-flagged 对象返回
UTC 字段,对本地对象返回本地字段 —— 因此 record 侧自动正确,
但**与本地 `DateTime` 比较的地方会错**。

需核对并修正的位置:

- 第 103 行 `_today`:由 `syncedAt` 构造,而 `syncedAt` 来自
  `watch_health_repository.dart:142` 的 `_clock()`(本地 `DateTime.now`)。
  `_today` 必须改为 UTC-flagged 才能与 record 正确比较。
- 第 106、235 行 `_sameDate(record.timestamp, ...)`
- 第 209-210 行 `record.timestamp.hour` 的时段过滤
- 第 230 行 `_today.subtract(...)`
- 第 258 行 `_sameDate()` 实现本身

修正原则:统一让参与比较的两侧都是 UTC-flagged,
即 `syncedAt` 侧也按墙上时钟口径构造。
实现时逐处核对,不做批量替换。

### 6.4 显示路径

`watch_health_page.dart:569` `_recordTime()`、第 78/124 行的
`_date()`/`_time()` 直接格式化 `DateTime` 字段,
在 UTC-flagged 对象上会输出正确的墙上时间,无需改动 —— 但需实测确认
未经由 `toLocal()` 中转。

## 7. 权衡与已接受的代价

**跨时区旅行时的不连续。** 手机下发新时区的墙上时钟秒时,秒数会跳变,
历史记录在跳变点出现重叠或空洞,且不同时区段的记录不能放在同一根
绝对时间轴上比较。

接受此代价的理由:每条记录始终等于"当时表盘显示的时间",永远可解释;
而替代方案(存真 UTC)在协议不携带时区的前提下,必须用固件里硬编码
的时区猜测去反推 UTC,一旦猜错就把错误永久固化进 Flash,且无法事后
恢复。本方案从不需要知道时区,因此从不会猜错。

**断裂式变更。** 新旧固件/手机混搭会静默偏一个时区,无报错。
已确认接受:当前处于开发阶段,设备可随时刷新固件,两侧同步发布。
文档中记载该不兼容性。协议不引入版本门(SPORT 命令已有 version=1
的先例,但本次不使用)。

**"全部用秒"有一处例外。** 闹钟因携带重复规则而保留日历字段
(§4.3),0x21 按键测试保留 2000 纪元(§4.4)。二者均在文档中显式标注。

## 8. 验证

### 8.1 往返一致性测试(两侧各一)

核心不变式:**手机折算出的秒数,经固件 `gmtime_r` 展开后,
必须还原出手机原始的本地日历字段。**

- 手机侧:扩充 `test/services/watch_time_protocol_test.dart`,
  断言给定本地时间生成的 4 字节秒数符合预期常数值。
- 固件侧:断言该秒数经 `gmtime_r` 得回相同的年月日时分秒。
- 手机侧:扩充 `test/services/watch_health_protocol_test.dart`,
  断言 `_readTimestamp()` 对给定秒数返回的 `DateTime` 字段
  与预期墙上时间一致,且不随运行环境时区变化。

后一条尤其重要:测试必须在任意 `TZ` 下通过,
这正是"时区不进入数据流"的可执行定义。

### 8.2 缺陷回归确认

- **跨天时刻**:验证 `EVT_TIME_DAY_CHANGED` 在墙上时钟 00:00 触发,
  而非 16:00(§1.1 缺陷 1)。
- **同步日志**:验证 SPORT 同步日志打印的时间与表盘一致,
  不再偏 8 小时(§1.1 缺陷 2)—— 这是最廉价的端到端验证点。
- **刻钟边界**:确认 `EVT_TIME_TICK_15MIN` 行为未退化。

### 8.3 现存数据兼容

因 `ts_utc` → `ts` 是纯改名且旧值与新语义数值相同(§3.1 理由 3、
§5.1),Flash 中既有记录应能被新固件正确读出并上报。
需实测确认一次。
