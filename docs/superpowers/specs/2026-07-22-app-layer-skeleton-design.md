# App layer skeleton design (Bluetooth smartwatch)

- **Date**: 2026-07-22
- **Branch**: `RTL8773G-eBadge-WH` (reuses the eBadge hardware environment; no eBadge business is implemented)
- **Deliverable scope**: skeleton code under `applications/app/` (headers + empty `.c`)
- **Product positioning**: full-featured Bluetooth smartwatch (multimedia + calls + health + notifications + in-house companion phone App)
- **This spec covers the skeleton only** —— each module's business logic (BLE private protocol, health algorithms, call state machine, etc.) is **out of scope here**; every module will get its own brainstorm later.

---

## 1. Background and goals

### 1.1 Existing low-level capabilities

A survey of `applications/` confirmed the following are available:

| Layer | Status |
|---|---|
| OS | Zephyr (the current platform, but the app layer does not assume Zephyr —— see §1.2) |
| BLE | Realtek BLE MGR + BR/EDR, A2DP / AVRCP / HFP / PAN already enabled |
| GUI | Realtek HoneyGUI, event driven, `gui_server_init()` already started in `main.c` |
| Sensor algorithms | `component/gsensor-algorithm/` has a step-counting FSM + `pedo_logger` + a shared worker slot |
| Storage | FlashDB in all three modes (KV / TS / Blob) ready, including the registry |
| RTC | enabled |
| Input | keys; touch is paused |
| OS abstraction | Realtek OSIF (`os_task.h` / `os_msg.h` / `os_timer.h` / `os_sync.h` etc.) —— portable across the Realtek SDK |

### 1.2 Platform-independence constraints

The user stated explicitly: **"it may not be on the Zephyr platform"**. This skeleton therefore:

- does **not use** Zephyr APIs (`k_thread` / `k_msgq` / `SYS_INIT` / `LOG_MODULE_REGISTER` etc.)
- does **not use** Zephyr's linker-section registration mechanism
- **uses** Realtek OSIF as its OS dependency; it is portable across the Realtek SDK (present on 8762D / 8762C / 8762E / 8773G / RTL87x3ep)

### 1.3 Goals

Establish an extensible app-layer foundation under `applications/app/`:

1. a unified module lifecycle contract
2. a unified inter-module communication mechanism (event bus)
3. header seams for 9 business submodules (public API fixed, implementation left empty)
4. reuse of the existing CMake build system, with minimal changes to the `main.c` mount point

**When the skeleton is done**: it builds, it runs after power-on, each module's init/start is called (observable in the logs), and the event bus API is usable at compile time (the internals stay TODO).

### 1.4 Non-goals

The following are **explicitly out of scope for this spec**:

- any module's business implementation (BLE private protocol, GATT services, call state machine, health algorithm calls, notification fetching, time sync protocol, OTA channel, UI interaction)
- UI layer design —— the user confirmed they will handle the UI framework themselves
- Kconfig options —— every module is compiled in unconditionally
- shell command wrapper (`app_shell`) —— rejected; business modules use Zephyr `SHELL_CMD_REGISTER` directly
- log wrapper (`app_log`) —— rejected; business modules use Zephyr LOG or `DBG_DIRECT` directly
- OTA (`app_ota`) —— removed from this skeleton round and brainstormed separately later; the `0x900` event id range stays reserved
- OSAL abstraction layer —— rejected; Realtek OSIF is used directly
- eBadge features —— explicitly ignored
- unit tests —— nothing to test while the skeleton has no implementation

---

## 2. Top-level design decisions (Q&A summary)

| Decision | Choice | Alternatives | Rationale |
|---|---|---|---|
| Inter-module communication | **event bus** (publish/subscribe) | direct function calls / OS msgq | at least 5 subsystems have one-to-many listeners (BLE state, battery, time…); avoids circular includes |
| Lifecycle contract | **registry + explicit init list** | linker-section auto registration / `SYS_INIT` | the module count is small (9) and an explicit list is easy to debug; platform independent |
| OS abstraction | **Realtek OSIF directly** | own OSAL / CMSIS-RTOS2 / POSIX | specified by the user; OSIF is portable across the Realtek SDK |
| Event callback threading model | **all callbacks serialised on a single dispatcher thread** | one msgq per module, dispatcher delivers | simple skeleton; modules that want async can forward themselves |
| `init` failure policy | **skip that module's start and continue overall** | abort on any failure | a watch product should degrade gracefully |
| Event payload limit | **32 bytes** | 64 / 128 / variable length | enough for state-type events; bulk data goes through getters or direct calls |
| Subscription table capacity | **32** | 16 / 64 | 8 business modules × 3~4 subscriptions on average ≈ 24~32, with headroom |
| ISR publish support | **keep** `app_event_publish_isr` | drop it | explicitly requested by the user |
| `EVT_SETTING_CHANGED` payload | **key enum** | key string pointer | stable lifetime, no string ownership issues |
| Notification item size | body 64 bytes | 128 bytes | each ~136B × 20 = ~2.7KB, memory friendly |
| `main.c` mount point | **after `gui_server_init()`** | before | the UI is ready first, so early events are not missed (minor concern; the event queue buffers anyway) |
| include paths | **flat** (`#include "app_health.h"`) | prefixed (`app_health/app_health.h`) | follows the existing project style |
| sync APIs during the skeleton phase | **empty implementations returning 0 / -1** | declare only, no implementation | builds right away |

---

## 3. Directory layout

```
applications/app/
├── CMakeLists.txt              # follows the port/CMakeLists.txt template; recursive add_subdirectory
├── app_core/
│   ├── CMakeLists.txt
│   ├── app_core.h              # public: app_core_init() / app_core_start()
│   ├── app_core.c              # iterate modules[], run init then start
│   ├── app_module.h            # app_module_t contract definition
│   ├── app_modules.c           # central modules[] list of the 9 modules
│   ├── app_event.h             # event bus API
│   ├── app_event.c             # dispatcher task + subscription table implementation
│   └── app_event_defs.h        # event id enum + payload struct definitions
├── app_ble/         { CMakeLists.txt, app_ble.h,     app_ble.c }
├── app_health/      { CMakeLists.txt, app_health.h,  app_health.c }
├── app_media/       { CMakeLists.txt, app_media.h,   app_media.c }
├── app_phone/       { CMakeLists.txt, app_phone.h,   app_phone.c }
├── app_notify/      { CMakeLists.txt, app_notify.h,  app_notify.c }
├── app_time/        { CMakeLists.txt, app_time.h,    app_time.c }
├── app_power/       { CMakeLists.txt, app_power.h,   app_power.c }
└── app_setting/     { CMakeLists.txt, app_setting.h, app_setting.c }
```

**Directory design notes**:

- every module gets its **own subdirectory**, even when the skeleton phase has just one `.c/.h` —— future internal splits (e.g. `app_health/hr.c`, `app_ble/gatt_watch.c`) need no move
- `app_core/` is a module directory too, so all subdirectories under `app/` have the same shape
- `app_modules.c` **is the only place that includes every submodule header**; adding or removing a module touches only this file
- `app_event.h` (mechanism) and `app_event_defs.h` (contract) are kept apart —— the latter grows frequently with the business, and changing it does not trigger a whole-project rebuild
- **no** `app/include/` aggregate header —— avoids "change one module header, rebuild the whole project"

---

## 4. `app_core` detailed design

### 4.1 Module contract (`app_module.h`)

```c
#ifndef __APP_MODULE_H__
#define __APP_MODULE_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef struct app_module {
    const char *name;              /* for logs / debugging, e.g. "health" */
    int  (*init)(void);            /* phase 1: subscribe to events, allocate resources; no event publishing, no threads */
    int  (*start)(void);           /* phase 2: start threads, begin work. All modules are init'ed, events may be published */
    void (*stop)(void);            /* optional, NULL means shutdown is unsupported. Reserved for low power / degraded mode */
} app_module_t;

#ifdef __cplusplus
}
#endif

#endif /* __APP_MODULE_H__ */
```

**Contract rules (written into the header comments as the specification; no runtime enforcement)**:

- `init` **must not publish events** —— some modules may not have subscribed yet at that point
- `init` **must not start threads or do I/O** —— only subscribe and pre-allocate resources
- `init` returns 0 on success and non-zero on failure; on non-zero `app_core` **skips that module's start** but keeps processing the others
- anything goes in `start`
- `stop` may be NULL; during the skeleton phase every module's stop is NULL or an empty implementation

### 4.2 Module list (`app_modules.c`)

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

/* order = startup dependency order: infrastructure → business features */
const app_module_t *const app_module_list[] = {
    &app_time_module,      /* time semantics; other modules depend on it for timestamps */
    &app_power_module,     /* battery monitoring, the earlier the better */
    &app_setting_module,   /* settings read/write; other modules read their defaults during init */
    &app_ble_module,       /* Bluetooth stack, the basis for later business communication */
    &app_health_module,    /* depends on time + setting */
    &app_media_module,     /* depends on ble */
    &app_phone_module,     /* depends on ble */
    &app_notify_module,    /* depends on ble */
};

const size_t app_module_count =
    sizeof(app_module_list) / sizeof(app_module_list[0]);
```

**Constraints**:

- **adding/removing a module only touches this file plus that module's CMakeLists (auto-globbed; in practice just create the subdirectory + `CMakeLists.txt`)**
- the list order is the startup order, so changing the order changes the dependencies —— the order itself is the documentation

### 4.3 Event bus (`app_event.h` + `app_event.c`)

**Public API (`app_event.h`)**:

```c
#ifndef __APP_EVENT_H__
#define __APP_EVENT_H__

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint16_t app_event_id_t;   /* concrete ids are defined in app_event_defs.h */

/* event callback; invoked in the dispatcher thread context */
typedef void (*app_event_cb_t)(app_event_id_t id,
                                const void *payload, size_t len,
                                void *user);

/* subscribe —— only allowed during module.init() */
int app_event_subscribe  (app_event_id_t id, app_event_cb_t cb, void *user);
int app_event_unsubscribe(app_event_id_t id, app_event_cb_t cb);

/* publish —— the payload is copied into the queue; the caller need not keep it alive */
int app_event_publish    (app_event_id_t id, const void *payload, size_t len);

/* publish from ISR context —— uses the ISR variant of os_msg_send underneath */
int app_event_publish_isr(app_event_id_t id, const void *payload, size_t len);

/* called once by app_core inside app_core_init(); business modules need not call it */
int app_event_bus_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_EVENT_H__ */
```

**Internal implementation constraints (written into the top-of-file comment of the `.c`)**:

- **one dispatcher task** (created with `os_task_create`, 2048B stack, medium priority —— exact values decided during implementation)
- **one `os_msg_queue`** receiving all events, queue depth 32
- **payload limit 32 bytes** (`APP_EVENT_MAX_PAYLOAD`) —— the event struct is `{id, len, u8 payload[32]}`
- **subscription table: static array, capacity 32** (`APP_EVENT_MAX_SUBS`) —— overflow returns `-ENOSPC` and logs
- **callbacks run serially on the dispatcher thread** —— all subscribers share one execution context; long jobs are forbidden inside callbacks and should be forwarded to the module's own task
- **publishing is asynchronous** —— `publish` only enqueues and returns; it does not wait for the callbacks to finish
- **oversized payloads / bulk data transfers**: **do not go through the event bus**; use getters or direct inter-module function calls (examples: AVRCP metadata strings, notification details, OTA data streams)

**Deliberately omitted**:

- priority events
- wildcard subscriptions
- event history/replay
- synchronous dispatch (if you want synchronous, call the function)

### 4.4 Event contract (`app_event_defs.h`)

```c
#ifndef __APP_EVENT_DEFS_H__
#define __APP_EVENT_DEFS_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* event ids are segmented per module (0x100 per segment) so an id reveals its origin at a glance */
enum {
    /* system level */
    EVT_SYS_READY          = 0x0001,   /* broadcast after all module.start() have completed */

    /* BLE (0x100~) */
    EVT_BLE_CONNECTED      = 0x0100,
    EVT_BLE_DISCONNECTED   = 0x0101,

    /* Power (0x200~) */
    EVT_POWER_LEVEL        = 0x0200,   /* payload: app_evt_power_level_t */
    EVT_POWER_LOW          = 0x0201,   /* payload: none */
    EVT_POWER_CHARGING     = 0x0202,   /* payload: app_evt_power_charging_t */

    /* Time (0x300~) */
    EVT_TIME_SYNCED        = 0x0300,   /* payload: none */
    EVT_TIME_TICK_MIN      = 0x0301,   /* payload: none */

    /* Setting (0x400~) */
    EVT_SETTING_CHANGED    = 0x0400,   /* payload: app_evt_setting_changed_t */

    /* Health (0x500~) */
    EVT_HEALTH_STEPS_UPDATED = 0x0500, /* payload: uint32_t steps */
    EVT_HEALTH_HR_UPDATED    = 0x0501, /* payload: uint8_t bpm */
    EVT_HEALTH_SPO2_UPDATED  = 0x0502, /* payload: uint8_t percent */

    /* Media (0x600~) */
    EVT_MEDIA_STATE_CHANGED  = 0x0600, /* payload: app_media_state_t */
    EVT_MEDIA_METADATA       = 0x0601, /* payload: none, use the getter for details */
    EVT_MEDIA_VOLUME_CHANGED = 0x0602, /* payload: uint8_t percent */

    /* Phone (0x700~) */
    EVT_PHONE_STATE_CHANGED  = 0x0700, /* payload: app_phone_state_t */
    EVT_PHONE_RING           = 0x0701, /* payload: none */

    /* Notify (0x800~) */
    EVT_NOTIFY_NEW           = 0x0800, /* payload: uint32_t id */
    EVT_NOTIFY_REMOVED       = 0x0801, /* payload: uint32_t id */
    EVT_NOTIFY_CLEARED       = 0x0802, /* payload: none */

    /* the 0x900~ segment is reserved for a future OTA */
};

/* Payload structs —— all cross-module payload definitions live here */

typedef struct { uint8_t percent; } app_evt_power_level_t;
typedef struct { bool    charging; } app_evt_power_charging_t;

/* Setting key enum —— used as the EVT_SETTING_CHANGED payload instead of a key string
 * when adding a key, add its enum here and its name mapping in app_setting.c */
typedef enum {
    APP_SETTING_KEY_UNKNOWN = 0,
    /* no concrete keys during the skeleton phase; added when a business module needs them */
} app_setting_key_id_t;

typedef struct { app_setting_key_id_t key; } app_evt_setting_changed_t;

#ifdef __cplusplus
}
#endif

#endif /* __APP_EVENT_DEFS_H__ */
```

### 4.5 `app_core` entry (`app_core.h` + `app_core.c`)

**`app_core.h`**:

```c
#ifndef __APP_CORE_H__
#define __APP_CORE_H__

#ifdef __cplusplus
extern "C" {
#endif

/* main.c only needs to see these two functions */
int app_core_init (void);   /* start the event bus + iterate modules[] running init */
int app_core_start(void);   /* iterate modules[] running start, then broadcast EVT_SYS_READY */

#ifdef __cplusplus
}
#endif

#endif /* __APP_CORE_H__ */
```

**`app_core.c` skeleton pseudo-flow**:

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

`mark_skipped` / `is_skipped`: use a static bitmap (9 bits are enough); the skeleton implementation is 4~5 lines of code.

---

## 5. Header seams of the 9 business modules

**General principles**:

- each module header exposes only 3 kinds of things: ① `extern const app_module_t app_xxx_module;` ② a few **synchronous query/operation APIs** ③ the module-specific type definitions
- **all asynchronous notification goes through the event bus**; headers **do not expose callback registration functions**
- during the skeleton phase the synchronous APIs in `.c` return 0 / -1 / NULL, so it builds right away

### 5.1 `app_ble` — Bluetooth stack + connection state

```c
extern const app_module_t app_ble_module;

bool app_ble_is_connected(void);
int  app_ble_disconnect(void);
int  app_ble_advertising_start(void);
int  app_ble_advertising_stop(void);
```

Events: `EVT_BLE_CONNECTED` / `EVT_BLE_DISCONNECTED`.

**Not covered by the skeleton**: GATT service definitions, pairing state machine, whitelist —— added when the module is split into files later.

### 5.2 `app_health` — health data aggregation

```c
extern const app_module_t app_health_module;

uint32_t app_health_steps_today(void);
uint8_t  app_health_heart_rate_last(void);   /* 0 = no valid value */
uint8_t  app_health_spo2_last(void);         /* 0 = no valid value */

int  app_health_measure_hr_start(void);
int  app_health_measure_hr_stop(void);
```

Events: `EVT_HEALTH_STEPS_UPDATED` / `EVT_HEALTH_HR_UPDATED` / `EVT_HEALTH_SPO2_UPDATED`.

**Not covered by the skeleton**: sensor drivers (port/component layer), algorithm calls (component layer), health data GATT service (inside app_ble).

### 5.3 `app_media` — A2DP playback + AVRCP

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

/* metadata strings are owned by app_media; the pointers stay valid until the next EVT_MEDIA_METADATA */
const char *app_media_track_title(void);
const char *app_media_track_artist(void);
```

Events: `EVT_MEDIA_STATE_CHANGED` / `EVT_MEDIA_METADATA` / `EVT_MEDIA_VOLUME_CHANGED`.

### 5.4 `app_phone` — HFP calls

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

/* number/contact strings are owned by app_phone; the pointers stay valid until the state changes */
const char *app_phone_peer_number(void);
const char *app_phone_peer_name(void);
```

Events: `EVT_PHONE_STATE_CHANGED` / `EVT_PHONE_RING`.

### 5.5 `app_notify` — notification forwarding

```c
extern const app_module_t app_notify_module;

#define APP_NOTIFY_MAX 20      /* number of ring buffer entries */

typedef struct {
    uint32_t id;
    uint32_t timestamp;        /* unix time */
    uint8_t  category;         /* see ANCS category */
    char     app[24];
    char     title[48];
    char     body[64];
} app_notify_item_t;

size_t app_notify_count(void);
const app_notify_item_t *app_notify_get(size_t index);   /* 0 = newest */
int    app_notify_dismiss(uint32_t id);
int    app_notify_clear_all(void);
```

Events: `EVT_NOTIFY_NEW` / `EVT_NOTIFY_REMOVED` / `EVT_NOTIFY_CLEARED` —— the payload carries only the id; details go through `app_notify_get()`.

**Memory estimate**: one item 4 + 4 + 1 + 24 + 48 + 64 = 145 bytes (~152B with alignment), ×20 ≈ 3KB.

### 5.6 `app_time` — time semantics layer

```c
extern const app_module_t app_time_module;

uint32_t app_time_now(void);                       /* unix seconds */
int16_t  app_time_tz_offset(void);                 /* minutes, Beijing = +480 */
int      app_time_tz_set(int16_t minutes);

int      app_time_set_from_phone(uint32_t unix_sec, int16_t tz_min);

typedef struct {
    uint16_t year;  uint8_t month; uint8_t day;
    uint8_t  hour;  uint8_t  min;   uint8_t sec;
    uint8_t  weekday;   /* 0=Sunday */
} app_time_local_t;

void app_time_local_now(app_time_local_t *out);
```

Events: `EVT_TIME_SYNCED` / `EVT_TIME_TICK_MIN`.

**Not covered by the skeleton**: alarms and schedules —— brainstormed separately later.

### 5.7 `app_power` — battery and charging

```c
extern const app_module_t app_power_module;

uint8_t app_power_battery_percent(void);
bool    app_power_is_charging(void);
```

Events: `EVT_POWER_LEVEL` / `EVT_POWER_LOW` / `EVT_POWER_CHARGING`.

### 5.8 `app_setting` — thin KV shell (ends up in FlashDB)

```c
extern const app_module_t app_setting_module;

/* Key naming convention (documented only, no runtime validation):
 *   dotted style, e.g. "user.age" / "display.brightness" / "vibrate.enable" */

int  app_setting_get_u32 (const char *key, uint32_t *out, uint32_t default_val);
int  app_setting_set_u32 (const char *key, uint32_t val);
int  app_setting_get_str (const char *key, char *out, size_t out_size, const char *default_val);
int  app_setting_set_str (const char *key, const char *val);
int  app_setting_get_blob(const char *key, void *out, size_t *inout_size);
int  app_setting_set_blob(const char *key, const void *val, size_t size);
int  app_setting_del     (const char *key);
```

Events: `EVT_SETTING_CHANGED`, payload = `app_evt_setting_changed_t { app_setting_key_id_t key; }`.

**Requirement**: `app_setting.c` maintains a `key string → key enum` mapping table; after a `set_*` successfully writes to FlashDB it publishes `EVT_SETTING_CHANGED` according to that mapping. During the skeleton phase the mapping table is empty and no real mapping happens (the enum only has `APP_SETTING_KEY_UNKNOWN`).

---

## 6. CMakeLists integration

### 6.1 Reuse the existing generic template

`applications/port/CMakeLists.txt` is already a generic template: glob `.c/.cpp/.h`, feed them to `${APP_TARGET_NAME}`, add the current directory to the include path, recurse with add_subdirectory. `component/CMakeLists.txt` uses the same template plus a skip list.

**This skeleton uses the same template**:
- `app/CMakeLists.txt`: exactly the `port/CMakeLists.txt` shape (only recurse into subdirectories when there is no `.c`)
- each module's `app/<module>/CMakeLists.txt`: the same template, taking the `SOURCES AND HEADERS` branch

**10 new CMakeLists in total** (one for `app/` plus one per module), **with essentially identical content**.

**No skip list** —— the app layer is all first-party modules, with no vendored third-party libraries.

### 6.2 The root CMakeLists needs no change

The `foreach(subdir ${SUBDIRS})` at the end of `applications/CMakeLists.txt` already `add_subdirectory`s any subdirectory containing a `CMakeLists.txt` —— **as long as `app/` has a `CMakeLists.txt`, it takes effect automatically**.

### 6.3 Include path strategy

Each module's CMakeLists adds **its own directory** to the include path. Other modules therefore include **flat**:

```c
#include "app_health.h"          /* correct */
```

There is no need for `#include "app_health/app_health.h"` —— this follows the existing project style (e.g. `app_lower_init.h`, `flash_map.h`).

The includes of every submodule header inside `app_modules.c` are flat as well.

---

## 7. `main.c` mount point

**The change is only 3 lines**, placed **after** `gui_server_init()`:

```c
#include "app_core.h"     /* new */

/* ...existing code unchanged... */
gui_server_init();
gui_set_keep_active_time(10000000);

app_core_init();          /* new */
app_core_start();         /* new */

return 0;
```

**About `main()` exiting**: after `main` returns the Zephyr main thread terminates, and events keep being driven by each module's own task and the event bus dispatcher task —— this already matches the current behaviour of `main.c`, so no extra handling is needed.

---

## 8. Skeleton completion verification

Once the skeleton is implemented the following should be observable:

1. **It builds** —— no undefined symbols, nothing left unlinked
2. **Startup logs** in this order:
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
   ... (the 8 modules start in turn, skipping failures)
   [app_core] EVT_SYS_READY published
   ```
3. **The system keeps running** —— `main` exits and the dispatcher task stays alive waiting for events
4. **The event bus API is callable but does not actually dispatch** —— `app_event_publish` currently returns 0 without triggering callbacks (the dispatcher internals are still TODO); this is an **acceptable state** during the skeleton phase, and the real implementation is left to a later plan

---

## 9. Explicit exclusions at a glance

| Item | Status | Reason |
|---|---|---|
| `app_ota/` | removed | the user explicitly said "no OTA for now"; the `0x900` event id range stays reserved |
| `app_shell/` | removed | rejected by the user; Zephyr `SHELL_CMD_REGISTER` is used directly |
| `app_log/` | removed | rejected by the user; Zephyr LOG / `DBG_DIRECT` is used directly |
| OSAL layer | not built | the user specified Realtek OSIF |
| Kconfig switches | not added | YAGNI; modules are compiled in unconditionally |
| UI layer | not involved | handled by the user |
| eBadge features | not involved | only the hardware is reused |
| Business implementations | not involved | outside the skeleton spec; each module gets its own brainstorm |
| Unit tests | not added | nothing to test while the skeleton has no implementation |

---

## 10. Follow-up roadmap (not promised by this spec)

Each business module will later go through its own brainstorm → spec → plan → implement round; suggested order:

1. `app_core` event bus **actual implementation** (a TODO during the skeleton phase)
2. `app_ble` private GATT service + phone App data sync protocol
3. `app_time` time sync (depends on 2)
4. `app_setting` wired up to FlashDB
5. `app_health` aggregation layer + step-counting integration
6. `app_notify` ANCS + in-house channel
7. `app_phone` HFP state machine
8. `app_media` A2DP/AVRCP
9. `app_power` battery algorithm and low-battery policy
10. `app_ota` comes back in via its own brainstorm

---

## Appendix A: Skeleton file inventory

New files in total: **10 CMakeLists + 8 `app_core/` files + 8×2=16 business module `.h/.c` files** = **34**.

Modified files: **`applications/main.c`**, +3 lines only.

No deletions.

## Appendix B: Consistency with the existing code style

- header guards use `#ifndef __XXX_H__` (aligned with `app_lower_init.h`)
- `extern "C"` wrapping included (aligned with the existing mixed C/C++ need)
- no separate copyright block at the top (aligned with `app_lower_init.h`)
- generic CMakeLists template (aligned with `port/CMakeLists.txt` / `component/CMakeLists.txt`)
- flat include paths (aligned with `flash_map.h` / `app_lower_init.h`)
