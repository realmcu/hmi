# App 层骨架设计（蓝牙智能手表）

- **日期**：2026-07-22
- **分支**：`RTL8773G-eBadge-WH`（复用 eBadge 硬件环境，不做 eBadge 业务）
- **产物范围**：`applications/app/` 目录下的骨架代码（header + 空 `.c`）
- **产品定位**：全功能蓝牙智能手表（多媒体 + 通话 + 健康 + 通知 + 自研配套手机 App）
- **本 spec 只覆盖骨架**——每个模块的业务逻辑（BLE 私有协议、健康算法、通话状态机等）**不在本 spec 范围内**，将来每个模块各自 brainstorm。

---

## 1. 背景与目标

### 1.1 现有底层能力

调研 `applications/` 已确认可用：

| 层 | 现状 |
|---|---|
| OS | Zephyr（当前平台，但 app 层不假设 Zephyr——见 §1.2） |
| BLE | Realtek BLE MGR + BR/EDR，A2DP / AVRCP / HFP / PAN 已开 |
| GUI | Realtek HoneyGUI，事件驱动，`gui_server_init()` 已在 `main.c` 起用 |
| 传感器算法 | `component/gsensor-algorithm/` 有计步 FSM + `pedo_logger` + 共享 worker slot |
| 存储 | FlashDB 三种模式（KV / TS / Blob）已就绪，含 registry |
| RTC | 开 |
| NN | TFLite Micro + CMSIS-NN 编译期可选（当前关闭，预留） |
| 输入 | 按键；触摸暂停用 |
| OS 抽象 | Realtek OSIF（`os_task.h` / `os_msg.h` / `os_timer.h` / `os_sync.h` 等）——跨 Realtek SDK 通用 |

### 1.2 平台无关性约束

用户明确：**「有可能不是在 Zephyr 平台」**。因此本骨架：

- **不使用** Zephyr API（`k_thread` / `k_msgq` / `SYS_INIT` / `LOG_MODULE_REGISTER` 等）
- **不使用** Zephyr 特有的 linker section 注册机制
- **使用** Realtek OSIF 作为 OS 依赖，它跨 Realtek SDK 通用（8762D / 8762C / 8762E / 8773G / RTL87x3ep 均有）

### 1.3 目标

在 `applications/app/` 下建立一套可扩展的 app 层地基：

1. 统一的模块生命周期契约
2. 统一的模块间通信机制（事件总线）
3. 9 个业务子模块的 header 接缝（对外 API 定型，实现留空）
4. 沿用现有 CMake 构建体系，`main.c` 挂载点最小改动

**骨架完工后**：编译能过、上电能跑，每个模块的 init/start 被调用（打印 log 可观察），事件总线 API 编译期可用（内部实现留 TODO）。

### 1.4 非目标

以下**明确不在本 spec 范围**：

- 任何模块的业务实现（BLE 私有协议、GATT service、通话状态机、健康算法调用、通知拉取、时间同步协议、OTA 通道、UI 交互）
- UI 层设计——用户已确认自己处理 UI 框架
- Kconfig 选项——所有模块无条件编入
- Shell 命令封装（`app_shell`）——已否决，业务模块直接用 Zephyr `SHELL_CMD_REGISTER`
- Log 封装（`app_log`）——已否决，业务模块直接用 Zephyr LOG 或 `DBG_DIRECT`
- OTA（`app_ota`）——已从本轮骨架移除，将来单独 brainstorm；事件 id 段 `0x900` 保留占位
- OSAL 抽象层——已否决，直接用 Realtek OSIF
- eBadge 功能——已明确忽略
- 单元测试——骨架期无实现可测

---

## 2. 顶层设计决策（Q&A 汇总）

| 决策点 | 选择 | 备选 | 理由 |
|---|---|---|---|
| 模块间通信 | **事件总线**（发布/订阅） | 直接函数调用 / OS msgq | 至少 5 个子系统存在一对多监听（BLE 状态、电量、时间…）；避免环形 include |
| 生命周期契约 | **注册表 + 显式 init 列表** | linker section 自动注册 / `SYS_INIT` | 模块规模小（9 个），显式列表调试友好；平台无关 |
| OS 抽象 | **直接用 Realtek OSIF** | 自建 OSAL / CMSIS-RTOS2 / POSIX | 用户已指定；OSIF 跨 Realtek SDK 通用 |
| 事件回调线程模型 | **所有回调在单一 dispatcher 线程串行** | 每模块一 msgq，dispatcher 分发 | 骨架简单；模块想异步再自己转发 |
| `init` 失败策略 | **跳过该模块的 start，整体继续** | 任一失败即停机 | 手表类产品应"降级可用" |
| 事件 payload 上限 | **32 字节** | 64 / 128 / 不定长 | 状态类事件够用；大数据走 getter 或直接调用 |
| 订阅表容量 | **32** | 16 / 64 | 8 业务模块 × 平均 3~4 订阅 ≈ 24~32，留富余 |
| ISR 发布支持 | **保留** `app_event_publish_isr` | 砍掉 | 用户显式要求 |
| `EVT_SETTING_CHANGED` payload | **key 枚举** | key 字符串指针 | 生命周期稳定，无字符串所有权问题 |
| 通知条目尺寸 | body 64 字节 | 128 字节 | 每条 ~136B × 20 = ~2.7KB，内存友好 |
| `main.c` 挂载位置 | **`gui_server_init()` 之后** | 之前 | UI 先就绪，避免早期事件错过（次要考虑，事件队列本身有缓冲） |
| include 路径 | **平铺**（`#include "app_health.h"`） | 带前缀（`app_health/app_health.h`） | 沿用项目现有风格 |
| 骨架期同步 API | **空实现返回 0 / -1** | 只声明不实现 | 骨架期即可编过 |

---

## 3. 目录结构

```
applications/app/
├── CMakeLists.txt              # 沿用 port/CMakeLists.txt 模板；递归 add_subdirectory
├── app_core/
│   ├── CMakeLists.txt
│   ├── app_core.h              # 对外：app_core_init() / app_core_start()
│   ├── app_core.c              # 遍历 modules[] 跑 init 再跑 start
│   ├── app_module.h            # app_module_t 契约定义
│   ├── app_modules.c           # 9 个模块的 modules[] 集中列表
│   ├── app_event.h             # 事件总线 API
│   ├── app_event.c             # dispatcher task + 订阅表实现
│   └── app_event_defs.h        # 事件 id 枚举 + payload struct 定义
├── app_ble/         { CMakeLists.txt, app_ble.h,     app_ble.c }
├── app_health/      { CMakeLists.txt, app_health.h,  app_health.c }
├── app_media/       { CMakeLists.txt, app_media.h,   app_media.c }
├── app_phone/       { CMakeLists.txt, app_phone.h,   app_phone.c }
├── app_notify/      { CMakeLists.txt, app_notify.h,  app_notify.c }
├── app_time/        { CMakeLists.txt, app_time.h,    app_time.c }
├── app_power/       { CMakeLists.txt, app_power.h,   app_power.c }
└── app_setting/     { CMakeLists.txt, app_setting.h, app_setting.c }
```

**目录设计要点**：

- 每个模块**独立子目录**，即使骨架期只有一份 `.c/.h`——将来模块内部拆分（如 `app_health/hr.c`、`app_ble/gatt_watch.c`）不用搬家
- `app_core/` 独立成模块目录，让 `app/` 下所有子目录形态一致
- `app_modules.c` **是唯一 include 全部子模块 header 的地方**，加/减模块只改这一处
- `app_event.h`（机制）与 `app_event_defs.h`（契约）分家——后者会随业务频繁增长，改它不会触发全项目重编译
- **不使用** `app/include/` 汇总头——避免"改一个模块 header 全项目重编译"

---

## 4. `app_core` 详细设计

### 4.1 模块契约（`app_module.h`）

```c
#ifndef __APP_MODULE_H__
#define __APP_MODULE_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef struct app_module {
    const char *name;              /* 用于 log / 调试，如 "health" */
    int  (*init)(void);            /* 阶段1：订阅事件、分配资源，禁止发布事件、禁止起线程 */
    int  (*start)(void);           /* 阶段2：起线程、开始工作。此时所有模块 init 完毕，可发布事件 */
    void (*stop)(void);            /* 可选，NULL 表示不支持关闭。留给低功耗/降级用 */
} app_module_t;

#ifdef __cplusplus
}
#endif

#endif /* __APP_MODULE_H__ */
```

**契约约束（写入 header 注释作为规范，不做运行时强制检查）**：

- `init` 中**禁止发布事件**——彼时可能仍有模块未订阅
- `init` 中**禁止起线程 / I/O**——只做订阅和资源预分配
- `init` 返回 0 为成功，非 0 为失败；`app_core` 遇非 0 会**跳过该模块的 start**，但继续处理其他模块
- `start` 中一切自由
- `stop` 允许为 NULL；骨架期所有模块 stop 都留 NULL 或空实现

### 4.2 模块列表（`app_modules.c`）

```c
#include "app_module.h"
#include "app_ble.h"
#include "app_time.h"
#include "app_power.h"
#include "app_setting.h"
#include "app_health.h"
#include "app_media.h"
#include "app_phone.h"
#include "app_notify.h"

#include <stddef.h>

/* 顺序 = 启动依赖顺序：基础设施 → 业务功能 */
const app_module_t *const app_module_list[] = {
    &app_time_module,      /* 时间语义化，其他模块打时间戳依赖它 */
    &app_power_module,     /* 电量监控，越早越好 */
    &app_setting_module,   /* 设置读写，供其他模块 init 时读默认配置 */
    &app_ble_module,       /* 蓝牙栈，后续业务通信基础 */
    &app_health_module,    /* 依赖 time + setting */
    &app_media_module,     /* 依赖 ble */
    &app_phone_module,     /* 依赖 ble */
    &app_notify_module,    /* 依赖 ble */
};

const size_t app_module_count =
    sizeof(app_module_list) / sizeof(app_module_list[0]);
```

**约束**：

- **加/减模块只改本文件 + 对应模块的 CMakeLists（自动 glob，实际上只需新建子目录 + `CMakeLists.txt`）**
- 列表顺序即启动顺序，改顺序即改依赖关系——顺序本身就是文档

### 4.3 事件总线（`app_event.h` + `app_event.c`）

**对外 API（`app_event.h`）**：

```c
#ifndef __APP_EVENT_H__
#define __APP_EVENT_H__

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint16_t app_event_id_t;   /* 具体 id 在 app_event_defs.h 定义 */

/* 事件回调；在 dispatcher 线程上下文调用 */
typedef void (*app_event_cb_t)(app_event_id_t id,
                                const void *payload, size_t len,
                                void *user);

/* 订阅——只允许在 module.init() 阶段调用 */
int app_event_subscribe  (app_event_id_t id, app_event_cb_t cb, void *user);
int app_event_unsubscribe(app_event_id_t id, app_event_cb_t cb);

/* 发布——payload 会被拷贝进队列；调用方不需保留 payload */
int app_event_publish    (app_event_id_t id, const void *payload, size_t len);

/* ISR 上下文发布——底层用 os_msg_send 的 ISR 变体 */
int app_event_publish_isr(app_event_id_t id, const void *payload, size_t len);

/* 由 app_core 在 app_core_init() 里调用一次；业务模块无需调用 */
int app_event_bus_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_EVENT_H__ */
```

**内部实现约束（写入 `.c` 顶部注释）**：

- **一个 dispatcher task**（用 `os_task_create` 创建，栈 2048B，优先级中等——具体数值实现期定）
- **一个 `os_msg_queue`** 收所有事件，队列深度 32
- **payload 上限 32 字节**（`APP_EVENT_MAX_PAYLOAD`）——事件结构体为 `{id, len, u8 payload[32]}`
- **订阅表：静态数组，容量 32**（`APP_EVENT_MAX_SUBS`）——超出返 `-ENOSPC` 并 log
- **回调在 dispatcher 线程串行执行**——所有订阅方共享同一执行上下文；回调内禁止长任务，长任务应转派到自己模块的 task
- **发布异步**——`publish` 只入队即返回，不等回调执行完
- **超长 payload / 大数据传输**：**不通过事件总线**，走 getter 或模块间直接函数调用（示例：AVRCP 元数据字符串、通知详情、OTA 数据流）

**刻意不做的**：

- 优先级事件
- 通配符订阅
- 事件历史/回放
- 同步派发（想同步就直接函数调用）

### 4.4 事件契约（`app_event_defs.h`）

```c
#ifndef __APP_EVENT_DEFS_H__
#define __APP_EVENT_DEFS_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 事件 id 按模块分段（每段 0x100 范围），一眼看到 id 知道来源 */
enum {
    /* 系统级 */
    EVT_SYS_READY          = 0x0001,   /* 所有 module.start() 完成后广播 */

    /* BLE (0x100~) */
    EVT_BLE_CONNECTED      = 0x0100,
    EVT_BLE_DISCONNECTED   = 0x0101,

    /* Power (0x200~) */
    EVT_POWER_LEVEL        = 0x0200,   /* payload: app_evt_power_level_t */
    EVT_POWER_LOW          = 0x0201,   /* payload: 无 */
    EVT_POWER_CHARGING     = 0x0202,   /* payload: app_evt_power_charging_t */

    /* Time (0x300~) */
    EVT_TIME_SYNCED        = 0x0300,   /* payload: 无 */
    EVT_TIME_TICK_MIN      = 0x0301,   /* payload: 无 */

    /* Setting (0x400~) */
    EVT_SETTING_CHANGED    = 0x0400,   /* payload: app_evt_setting_changed_t */

    /* Health (0x500~) */
    EVT_HEALTH_STEPS_UPDATED = 0x0500, /* payload: uint32_t steps */
    EVT_HEALTH_HR_UPDATED    = 0x0501, /* payload: uint8_t bpm */
    EVT_HEALTH_SPO2_UPDATED  = 0x0502, /* payload: uint8_t percent */

    /* Media (0x600~) */
    EVT_MEDIA_STATE_CHANGED  = 0x0600, /* payload: app_media_state_t */
    EVT_MEDIA_METADATA       = 0x0601, /* payload: 无，getter 取详情 */
    EVT_MEDIA_VOLUME_CHANGED = 0x0602, /* payload: uint8_t percent */

    /* Phone (0x700~) */
    EVT_PHONE_STATE_CHANGED  = 0x0700, /* payload: app_phone_state_t */
    EVT_PHONE_RING           = 0x0701, /* payload: 无 */

    /* Notify (0x800~) */
    EVT_NOTIFY_NEW           = 0x0800, /* payload: uint32_t id */
    EVT_NOTIFY_REMOVED       = 0x0801, /* payload: uint32_t id */
    EVT_NOTIFY_CLEARED       = 0x0802, /* payload: 无 */

    /* 0x900~ 段保留给未来的 OTA */
};

/* Payload 结构 —— 所有跨模块 payload 定义集中于此 */

typedef struct { uint8_t percent; } app_evt_power_level_t;
typedef struct { bool    charging; } app_evt_power_charging_t;

/* Setting key 枚举 —— EVT_SETTING_CHANGED 的 payload 用它，而非 key 字符串
 * 添加新 key 时同时在此加枚举 + 在 app_setting.c 建立 key 名映射 */
typedef enum {
    APP_SETTING_KEY_UNKNOWN = 0,
    /* 骨架期不定义具体 key，等业务模块需要时补 */
} app_setting_key_id_t;

typedef struct { app_setting_key_id_t key; } app_evt_setting_changed_t;

#ifdef __cplusplus
}
#endif

#endif /* __APP_EVENT_DEFS_H__ */
```

### 4.5 `app_core` 入口（`app_core.h` + `app_core.c`）

**`app_core.h`**：

```c
#ifndef __APP_CORE_H__
#define __APP_CORE_H__

#ifdef __cplusplus
extern "C" {
#endif

/* main.c 只需要看到这两个函数 */
int app_core_init (void);   /* 起事件总线 + 遍历 modules[] 跑 init */
int app_core_start(void);   /* 遍历 modules[] 跑 start，最后广播 EVT_SYS_READY */

#ifdef __cplusplus
}
#endif

#endif /* __APP_CORE_H__ */
```

**`app_core.c` 骨架伪流程**：

```c
int app_core_init(void) {
    log("[app_core] init begin");
    app_event_bus_init();
    for (each m in app_module_list) {
        log("[app_core] init %s", m->name);
        if (m->init) {
            int rc = m->init();
            if (rc != 0) {
                log("[app_core] init %s failed rc=%d, will skip start", m->name, rc);
                mark_skipped(m);
            }
        }
    }
    return 0;
}

int app_core_start(void) {
    log("[app_core] start begin");
    for (each m in app_module_list) {
        if (is_skipped(m)) continue;
        log("[app_core] start %s", m->name);
        if (m->start) m->start();
    }
    app_event_publish(EVT_SYS_READY, NULL, 0);
    log("[app_core] EVT_SYS_READY published");
    return 0;
}
```

`mark_skipped` / `is_skipped`：用一个静态 bitmap（9 位足够），骨架实现 4~5 行代码即可。

---

## 5. 9 个业务模块的 header 接缝

**通用原则**：

- 每个模块 header 只暴露 3 类东西：① `extern const app_module_t app_xxx_module;` ② 少量**同步查询/操作 API** ③ 该模块特有的类型定义
- **异步通知全部走事件总线**，header **不暴露回调注册函数**
- 骨架期同步 API `.c` 里返回 0 / -1 / NULL 空实现，让骨架期即可编过

### 5.1 `app_ble` — 蓝牙栈 + 连接状态

```c
extern const app_module_t app_ble_module;

bool app_ble_is_connected(void);
int  app_ble_disconnect(void);
int  app_ble_advertising_start(void);
int  app_ble_advertising_stop(void);
```

事件：`EVT_BLE_CONNECTED` / `EVT_BLE_DISCONNECTED`。

**骨架不涵盖**：GATT service 定义、pairing 状态机、白名单——将来模块内拆分文件时加。

### 5.2 `app_health` — 健康数据聚合

```c
extern const app_module_t app_health_module;

uint32_t app_health_steps_today(void);
uint8_t  app_health_heart_rate_last(void);   /* 0 = 无有效值 */
uint8_t  app_health_spo2_last(void);         /* 0 = 无有效值 */

int  app_health_measure_hr_start(void);
int  app_health_measure_hr_stop(void);
```

事件：`EVT_HEALTH_STEPS_UPDATED` / `EVT_HEALTH_HR_UPDATED` / `EVT_HEALTH_SPO2_UPDATED`。

**骨架不涵盖**：传感器驱动（port/component 层）、算法调用（component 层）、健康数据 GATT service（app_ble 内）。

### 5.3 `app_media` — A2DP 播放 + AVRCP

```c
extern const app_module_t app_media_module;

typedef enum {
    APP_MEDIA_STATE_IDLE,
    APP_MEDIA_STATE_PLAYING,
    APP_MEDIA_STATE_PAUSED,
} app_media_state_t;

app_media_state_t app_media_state(void);

int  app_media_play(void);
int  app_media_pause(void);
int  app_media_next(void);
int  app_media_prev(void);
int  app_media_volume_set(uint8_t percent);
uint8_t app_media_volume_get(void);

/* 元数据字符串由 app_media 内部持有；指针有效性到下一次 EVT_MEDIA_METADATA */
const char *app_media_track_title(void);
const char *app_media_track_artist(void);
```

事件：`EVT_MEDIA_STATE_CHANGED` / `EVT_MEDIA_METADATA` / `EVT_MEDIA_VOLUME_CHANGED`。

### 5.4 `app_phone` — HFP 通话

```c
extern const app_module_t app_phone_module;

typedef enum {
    APP_PHONE_STATE_IDLE,
    APP_PHONE_STATE_INCOMING,
    APP_PHONE_STATE_OUTGOING,
    APP_PHONE_STATE_ACTIVE,
} app_phone_state_t;

app_phone_state_t app_phone_state(void);

int  app_phone_answer(void);
int  app_phone_hangup(void);
int  app_phone_reject(void);

/* 号码/联系人字符串由 app_phone 内部持有；指针到状态变更前有效 */
const char *app_phone_peer_number(void);
const char *app_phone_peer_name(void);
```

事件：`EVT_PHONE_STATE_CHANGED` / `EVT_PHONE_RING`。

### 5.5 `app_notify` — 通知转发

```c
extern const app_module_t app_notify_module;

#define APP_NOTIFY_MAX 20      /* 环形缓存条数 */

typedef struct {
    uint32_t id;
    uint32_t timestamp;        /* unix time */
    uint8_t  category;         /* 参考 ANCS category */
    char     app[24];
    char     title[48];
    char     body[64];
} app_notify_item_t;

size_t app_notify_count(void);
const app_notify_item_t *app_notify_get(size_t index);   /* 0 = 最新 */
int    app_notify_dismiss(uint32_t id);
int    app_notify_clear_all(void);
```

事件：`EVT_NOTIFY_NEW` / `EVT_NOTIFY_REMOVED` / `EVT_NOTIFY_CLEARED`——payload 只放 id，详情走 `app_notify_get()`。

**内存估算**：单条 4 + 4 + 1 + 24 + 48 + 64 = 145 字节（含对齐 ~152B），×20 ≈ 3KB。

### 5.6 `app_time` — 时间语义层

```c
extern const app_module_t app_time_module;

uint32_t app_time_now(void);                       /* unix seconds */
int16_t  app_time_tz_offset(void);                 /* 分钟，北京 = +480 */
int      app_time_tz_set(int16_t minutes);

int      app_time_set_from_phone(uint32_t unix_sec, int16_t tz_min);

typedef struct {
    uint16_t year;  uint8_t month; uint8_t day;
    uint8_t  hour;  uint8_t  min;   uint8_t sec;
    uint8_t  weekday;   /* 0=Sunday */
} app_time_local_t;

void app_time_local_now(app_time_local_t *out);
```

事件：`EVT_TIME_SYNCED` / `EVT_TIME_TICK_MIN`。

**骨架不涵盖**：闹钟、日程——将来单独 brainstorm。

### 5.7 `app_power` — 电量与充电

```c
extern const app_module_t app_power_module;

uint8_t app_power_battery_percent(void);
bool    app_power_is_charging(void);
```

事件：`EVT_POWER_LEVEL` / `EVT_POWER_LOW` / `EVT_POWER_CHARGING`。

### 5.8 `app_setting` — 薄壳 KV（最终调 FlashDB）

```c
extern const app_module_t app_setting_module;

/* Key 命名规范（写入注释，无运行时校验）：
 *   点分风格，如 "user.age" / "display.brightness" / "vibrate.enable" */

int  app_setting_get_u32 (const char *key, uint32_t *out, uint32_t default_val);
int  app_setting_set_u32 (const char *key, uint32_t val);
int  app_setting_get_str (const char *key, char *out, size_t out_size, const char *default_val);
int  app_setting_set_str (const char *key, const char *val);
int  app_setting_get_blob(const char *key, void *out, size_t *inout_size);
int  app_setting_set_blob(const char *key, const void *val, size_t size);
int  app_setting_del     (const char *key);
```

事件：`EVT_SETTING_CHANGED`，payload = `app_evt_setting_changed_t { app_setting_key_id_t key; }`。

**要求**：`app_setting.c` 内维护一个 `key 字符串 → key 枚举` 的映射表，`set_*` 写入 FlashDB 成功后按映射表发布 `EVT_SETTING_CHANGED`。骨架期映射表为空，不做实际映射（枚举里只有 `APP_SETTING_KEY_UNKNOWN`）。

---

## 6. CMakeLists 集成

### 6.1 沿用现有通用模板

`applications/port/CMakeLists.txt` 已是通用模板：glob `.c/.cpp/.h`、塞给 `${APP_TARGET_NAME}`、把当前目录加进 include path、递归 add_subdirectory。`component/CMakeLists.txt` 用同一模板加 skip list。

**本骨架沿用同一模板**：
- `app/CMakeLists.txt`：完全复用 `port/CMakeLists.txt` 的形态（无 `.c` 时只递归子目录）
- 每个模块 `app/<模块>/CMakeLists.txt`：同一份模板，走 `SOURCES AND HEADERS` 分支

**共计新增 10 份 CMakeLists**（`app/` 一份 + 9 个模块各一份），**内容基本一致**。

**不加 skip list**——app 层全是自写模块，无 vendored 三方库。

### 6.2 根 CMakeLists 无改动

`applications/CMakeLists.txt` 末尾的 `foreach(subdir ${SUBDIRS})` 已自动 `add_subdirectory` 任何含 `CMakeLists.txt` 的子目录——**只要 `app/` 下有 `CMakeLists.txt`，即自动生效**。

### 6.3 include 路径策略

各模块 CMakeLists 会把**自身目录**加进 include path。因此其他模块 include 时**平铺书写**：

```c
#include "app_health.h"          /* 正确 */
```

不需要 `#include "app_health/app_health.h"`——沿用项目现有风格（如 `app_lower_init.h`、`flash_map.h`）。

`app_modules.c` 内 include 所有子模块 header 亦平铺。

---

## 7. `main.c` 挂载点

**改动仅 3 行**，位置在 `gui_server_init()` **之后**：

```c
#include "app_core.h"     /* 新增 */

/* ...现有代码不变... */
gui_server_init();
gui_set_keep_active_time(10000000);

app_core_init();          /* 新增 */
app_core_start();         /* 新增 */

return 0;
```

**关于 `main()` 退出**：`main` 返回后 Zephyr 主线程终止，事件由各模块自己的 task 和事件总线的 dispatcher task 继续驱动——已符合当前 `main.c` 的行为，无需额外处理。

---

## 8. 骨架完工验证

骨架实现完毕后，应能观察到：

1. **编译通过**——无未定义符号、无未链接
2. **启动 log** 输出如下顺序：
   ```
   [app_core] init begin
   [app_core] init time
   [app_core] init power
   [app_core] init setting
   [app_core] init ble
   [app_core] init health
   [app_core] init media
   [app_core] init phone
   [app_core] init notify
   [app_core] start begin
   [app_core] start time
   ...（依次 8 个模块 start，跳过失败者）
   [app_core] EVT_SYS_READY published
   ```
3. **系统持续运行**——`main` 退出，dispatcher task 存活等事件
4. **事件总线 API 可调用但不实际派发**——`app_event_publish` 目前返回 0 而不触发回调（因为 dispatcher 内部实现仍是 TODO），这是骨架期的**允许状态**；真正实现留给后续 plan

---

## 9. 明确排除项一览

| 项 | 状态 | 原因 |
|---|---|---|
| `app_ota/` | 移除 | 用户明确"OTA 先不实现"；事件 id 段 `0x900` 保留占位 |
| `app_shell/` | 移除 | 用户否决；直接用 Zephyr `SHELL_CMD_REGISTER` |
| `app_log/` | 移除 | 用户否决；直接用 Zephyr LOG / `DBG_DIRECT` |
| OSAL 层 | 不建 | 用户指定用 Realtek OSIF |
| Kconfig 开关 | 不加 | YAGNI，模块无条件编入 |
| UI 层 | 不涉及 | 用户自处理 |
| eBadge 功能 | 不涉及 | 仅复用硬件 |
| 业务实现 | 不涉及 | 骨架 spec 范围外，各模块单独 brainstorm |
| 单元测试 | 不加 | 骨架期无实现可测 |

---

## 10. 后续路线（非本 spec 承诺）

每个业务模块将来会单独走一轮 brainstorm → spec → plan → implement，建议顺序：

1. `app_core` 事件总线**实际实现**（骨架期是 TODO）
2. `app_ble` 私有 GATT 服务 + 手机 App 数据同步协议
3. `app_time` 时间同步（依赖 2）
4. `app_setting` 与 FlashDB 打通
5. `app_health` 聚合层 + 计步接入
6. `app_notify` ANCS + 自研通道
7. `app_phone` HFP 状态机
8. `app_media` A2DP/AVRCP
9. `app_power` 电量算法与低电策略
10. `app_ota` 单独 brainstorm 回来加入

---

## 附录 A：骨架文件清单

新增文件总数：**10 份 CMakeLists + 8 份 `app_core/` 文件 + 8×2=16 份业务模块 `.h/.c`** = **34 份**。

修改文件：**`applications/main.c`** 仅 +3 行。

无删除。

## 附录 B：与项目现有代码风格的一致性

- Header guard 使用 `#ifndef __XXX_H__`（对齐 `app_lower_init.h`）
- 含 `extern "C"` 包裹（对齐现有 C++ 混编需求）
- 头部无独立版权声明块（对齐 `app_lower_init.h`）
- CMakeLists 通用模板（对齐 `port/CMakeLists.txt` / `component/CMakeLists.txt`）
- 平铺 include 路径（对齐 `flash_map.h` / `app_lower_init.h`）
