# App Layer Skeleton Design (Bluetooth Smartwatch)

- **Date**: 2026-07-22
- **Branch**: `RTL8773G-eBadge-WH` (reuses the eBadge hardware environment, without eBadge business logic)
- **Deliverable scope**: skeleton code under `applications/app/` (headers + empty `.c` files)
- **Product positioning**: full-featured Bluetooth smartwatch (multimedia + calls + health + notifications + a self-developed companion phone App)
- **This spec covers only the skeleton** -- the business logic of each module (BLE private protocol, health algorithms, call state machine, etc.) is **out of scope for this spec**; each module will have its own brainstorm later.

---

## 1. Background and Goals

### 1.1 Existing lower-layer capabilities

A survey of `applications/` confirms the following are available:

| Layer | Status |
|---|---|
| OS | Zephyr (current platform, but the app layer does not assume Zephyr -- see §1.2) |
| BLE | Realtek BLE MGR + BR/EDR, A2DP / AVRCP / HFP / PAN enabled |
| GUI | Realtek HoneyGUI, event-driven; `gui_server_init()` already started in `main.c` |
| Sensor algorithms | `component/gsensor-algorithm/` has the pedometer FSM + `pedo_logger` + shared worker slot |
| Storage | All three FlashDB modes (KV / TS / Blob) ready, including the registry |
| RTC | Enabled |
| Input | Keys; touch paused |
| OS abstraction | Realtek OSIF (`os_task.h` / `os_msg.h` / `os_timer.h` / `os_sync.h` etc.) -- common across the Realtek SDK |

### 1.2 Platform-independence constraints

The user made it explicit: **"we may not be on the Zephyr platform"**. Therefore this skeleton:

- Does **not use** Zephyr APIs (`k_thread` / `k_msgq` / `SYS_INIT` / `LOG_MODULE_REGISTER` etc.)
- Does **not use** Zephyr-specific linker section registration mechanisms
- **Uses** Realtek OSIF as the OS dependency; it is common across Realtek SDKs (available on 8762D / 8762C / 8762E / 8773G / RTL87x3ep)

### 1.3 Goals

Build an extensible app-layer foundation under `applications/app/`:

1. A unified module lifecycle contract
2. A unified inter-module communication mechanism (event bus)
3. Header seams for 9 business submodules (public APIs fixed, implementations left empty)
4. Reuse the existing CMake build system with minimal changes to the `main.c` mount point

**After the skeleton is complete**: it compiles and runs on power-on, each module's init/start is invoked (observable via logs), and the event bus API is available at build time (the internal implementation is left as a TODO).

### 1.4 Non-goals

The following are **explicitly out of scope for this spec**:

- The business implementation of any module (BLE private protocol, GATT service, call state machine, health algorithm invocation, notification fetching, time sync protocol, OTA channel, UI interaction)
- UI layer design -- the user has confirmed they handle the UI framework themselves
- Kconfig options -- all modules are compiled in unconditionally
- Shell command wrappers (`app_shell`) -- rejected; business modules use Zephyr `SHELL_CMD_REGISTER` directly
- Log wrappers (`app_log`) -- rejected; business modules use Zephyr LOG or `DBG_DIRECT` directly
- OTA (`app_ota`) -- removed from this skeleton round, to be brainstormed separately later; the event id range `0x900` is kept as a placeholder
- OSAL abstraction layer -- rejected; use Realtek OSIF directly
- eBadge features -- explicitly ignored
- Unit tests -- there is no implementation to test during the skeleton phase

---

## 2. Top-level Design Decisions (Q&A summary)

| Decision point | Choice | Alternatives | Rationale |
|---|---|---|---|
| Inter-module communication | **Event bus** (publish/subscribe) | Direct function calls / OS msgq | At least 5 subsystems have one-to-many listeners (BLE state, battery, time...); avoids circular includes |
| Lifecycle contract | **Registry + explicit init list** | Linker section auto-registration / `SYS_INIT` | Small module count (9); an explicit list is debugger-friendly and platform-independent |
| OS abstraction | **Use Realtek OSIF directly** | Self-built OSAL / CMSIS-RTOS2 / POSIX | User specified it; OSIF is common across Realtek SDKs |
| Event callback thread model | **All callbacks serialized on a single dispatcher thread** | One msgq per module, dispatcher distributes | Keeps the skeleton simple; modules can forward asynchronously if needed |
| `init` failure policy | **Skip that module's start, continue overall** | Stop on any failure | Watch-class products should "degrade gracefully" |
| Event payload limit | **32 bytes** | 64 / 128 / variable | Sufficient for state events; large data goes through getters or direct calls |
| Subscription table capacity | **32** | 16 / 64 | 8 business modules x average 3~4 subscriptions ≈ 24~32, leaving headroom |
| ISR publish support | **Keep** `app_event_publish_isr` | Drop it | Explicit user requirement |
| `EVT_SETTING_CHANGED` payload | **key enum** | key string pointer | Lifetime is stable; no string ownership issues |
| Notification entry size | body 64 bytes | 128 bytes | Each ≈136B x 20 = ≈2.7KB, memory-friendly |
| `main.c` mount point | **After `gui_server_init()`** | Before | UI is ready first, avoiding missed early events (minor concern; the event queue itself buffers) |
| include path | **Flat** (`#include "app_health.h"`) | Prefixed (`app_health/app_health.h`) | Follows the project's existing style |
| Skeleton-phase sync API | **Empty implementation returning 0 / -1** | Declare only, no implementation | Compiles during the skeleton phase |

---

## 3. Directory Structure

```
applications/app/
├── CMakeLists.txt              # follows the port/CMakeLists.txt template; recursive add_subdirectory
├── app_core/
│   ├── CMakeLists.txt
│   ├── app_core.h              # public: app_core_init() / app_core_start()
│   ├── app_core.c              # iterates modules[], running init then start
│   ├── app_module.h            # app_module_t contract definition
│   ├── app_modules.c           # central modules[] list of the 9 modules
│   ├── app_event.h             # event bus API
│   ├── app_event.c             # dispatcher task + subscription table implementation
│   └── app_event_defs.h        # event id enums + payload struct definitions
├── app_ble/         { CMakeLists.txt, app_ble.h,     app_ble.c }
├── app_health/      { CMakeLists.txt, app_health.h,  app_health.c }
├── app_media/       { CMakeLists.txt, app_media.h,   app_media.c }
├── app_phone/       { CMakeLists.txt, app_phone.h,   app_phone.c }
├── app_notify/      { CMakeLists.txt, app_notify.h,  app_notify.c }
├── app_time/        { CMakeLists.txt, app_time.h,    app_time.c }
├── app_power/       { CMakeLists.txt, app_power.h,   app_power.c }
└── app_setting/     { CMakeLists.txt, app_setting.h, app_setting.c }
```

**Directory design points**:

- Each module has its **own subdirectory**, even if the skeleton phase has only one `.c/.h` -- future splits inside a module (e.g. `app_health/hr.c`, `app_ble/gatt_watch.c`) will not need a move
- `app_core/` is its own module directory, so every subdirectory under `app/` has a consistent shape
- `app_modules.c` **is the only place that includes all submodule headers**; adding/removing a module only touches this one place
- `app_event.h` (mechanism) and `app_event_defs.h` (contract) are kept separate -- the latter will grow frequently with business logic, and changing it will not trigger a full rebuild
- **Do not use** a consolidated `app/include/` header -- this avoids "changing one module header rebuilds the whole project"

---

## 4. `app_core` Detailed Design

### 4.1 Module contract (`app_module.h`)

```c
#ifndef __APP_MODULE_H__
#define __APP_MODULE_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef struct app_module {
    const char *name;              /* for log / debug, e.g. "health" */
    int  (*init)(void);            /* phase 1: subscribe events, allocate resources; publishing events and starting threads are forbidden */
    int  (*start)(void);           /* phase 2: start threads, begin work. All modules have been initialized; events may be published */
    void (*stop)(void);            /* optional, NULL means shutdown unsupported. Reserved for low-power/degradation */
} app_module_t;

#ifdef __cplusplus
}
#endif

#endif /* __APP_MODULE_H__ */
```

**Contract constraints (written into header comments as a specification, not enforced at runtime)**:

- **Do not publish events** in `init` -- some modules may still be unsubscribed at that point
- **Do not start threads / do I/O** in `init` -- only subscribe and pre-allocate resources
- `init` returns 0 for success and non-zero for failure; on non-zero, `app_core` **skips that module's start** but continues with the other modules
- Everything is free in `start`
- `stop` may be NULL; during the skeleton phase all module `stop`s are NULL or empty

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

/* order = startup dependency order: infrastructure -> business features */
const app_module_t *const app_module_list[] = {
    &app_time_module,      /* time semantics; other modules depend on it for timestamps */
    &app_power_module,     /* battery monitoring, the earlier the better */
    &app_setting_module,   /* setting reads/writes, for other modules to read defaults during init */
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

- **Adding/removing a module only touches this file + the corresponding module's CMakeLists (automatic glob; in practice you only create a new subdirectory + `CMakeLists.txt`)**
- The list order is the startup order; changing the order changes the dependencies -- the order itself is documentation

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

/* subscribe -- only allowed during module.init() */
int app_event_subscribe  (app_event_id_t id, app_event_cb_t cb, void *user);
int app_event_unsubscribe(app_event_id_t id, app_event_cb_t cb);

/* publish -- the payload is copied into the queue; the caller need not keep it alive */
int app_event_publish    (app_event_id_t id, const void *payload, size_t len);

/* publish from ISR context -- uses the ISR variant of os_msg_send underneath */
int app_event_publish_isr(app_event_id_t id, const void *payload, size_t len);

/* called once by app_core inside app_core_init(); business modules need not call it */
int app_event_bus_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_EVENT_H__ */
```

**Internal implementation constraints (written into the top-of-file comment of `.c`)**:

- **One dispatcher task** (created with `os_task_create`, 2048B stack, medium priority -- exact values decided during implementation)
- **One `os_msg_queue`** receiving all events, queue depth 32
- **Payload limit 32 bytes** (`APP_EVENT_MAX_PAYLOAD`) -- the event struct is `{id, len, u8 payload[32]}`
- **Subscription table: static array, capacity 32** (`APP_EVENT_MAX_SUBS`) -- overflow returns `-ENOSPC` and logs
- **Callbacks run serially on the dispatcher thread** -- all subscribers share the same execution context; long tasks are forbidden in callbacks and should be forwarded to the module's own task
- **Publishing is asynchronous** -- `publish` only enqueues and returns, without waiting for callbacks to finish
- **Oversized payload / large data transfers**: **do not go through the event bus**; use getters or direct inter-module function calls (examples: AVRCP metadata strings, notification details, OTA data streams)

**Deliberately omitted**:

- Priority events
- Wildcard subscriptions
- Event history/replay
- Synchronous dispatch (if you want synchronous, just call the function directly)

### 4.4 Event contract (`app_event_defs.h`)

```c
#ifndef __APP_EVENT_DEFS_H__
#define __APP_EVENT_DEFS_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* event ids are segmented by module (0x100 range each), so the source is obvious at a glance */
enum {
    /* system level */
    EVT_SYS_READY          = 0x0001,   /* broadcast after all module.start() complete */

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

    /* the 0x900~ range is reserved for future OTA */
};

/* Payload structs -- all cross-module payload definitions are centralized here */

typedef struct { uint8_t percent; } app_evt_power_level_t;
typedef struct { bool    charging; } app_evt_power_charging_t;

/* Setting key enum -- used by the EVT_SETTING_CHANGED payload instead of key strings.
 * When adding a new key, add the enum here and build the key name mapping in app_setting.c */
typedef enum {
    APP_SETTING_KEY_UNKNOWN = 0,
    /* no concrete keys are defined during the skeleton phase; add them when business modules need them */
} app_setting_key_id_t;

typedef struct { app_setting_key_id_t key; } app_evt_setting_changed_t;

#ifdef __cplusplus
}
#endif

#endif /* __APP_EVENT_DEFS_H__ */
```

### 4.5 `app_core` entry points (`app_core.h` + `app_core.c`)

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

`mark_skipped` / `is_skipped`: use a static bitmap (9 bits is enough); about 4~5 lines of code for the skeleton implementation.

---

## 5. Header Seams for the 9 Business Modules

**General principles**:

- Each module header exposes only 3 kinds of things: (1) `extern const app_module_t app_xxx_module;` (2) a small number of **synchronous query/operation APIs** (3) type definitions specific to that module
- **All asynchronous notifications go through the event bus**; headers **do not expose callback registration functions**
- During the skeleton phase the synchronous APIs are empty implementations in `.c` returning 0 / -1 / NULL, so it compiles during the skeleton phase

### 5.1 `app_ble` -- Bluetooth stack + connection state

```c
extern const app_module_t app_ble_module;

bool app_ble_is_connected(void);
int  app_ble_disconnect(void);
int  app_ble_advertising_start(void);
int  app_ble_advertising_stop(void);
```

Events: `EVT_BLE_CONNECTED` / `EVT_BLE_DISCONNECTED`.

**Not covered by the skeleton**: GATT service definitions, pairing state machine, whitelist -- add these when splitting files inside the module later.

### 5.2 `app_health` -- health data aggregation

```c
extern const app_module_t app_health_module;

uint32_t app_health_steps_today(void);
uint8_t  app_health_heart_rate_last(void);   /* 0 = no valid value */
uint8_t  app_health_spo2_last(void);         /* 0 = no valid value */

int  app_health_measure_hr_start(void);
int  app_health_measure_hr_stop(void);
```

Events: `EVT_HEALTH_STEPS_UPDATED` / `EVT_HEALTH_HR_UPDATED` / `EVT_HEALTH_SPO2_UPDATED`.

**Not covered by the skeleton**: sensor drivers (port/component layer), algorithm invocation (component layer), health-data GATT service (inside app_ble).

### 5.3 `app_media` -- A2DP playback + AVRCP

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

/* metadata strings are owned internally by app_media; the pointers are valid until the next EVT_MEDIA_METADATA */
const char *app_media_track_title(void);
const char *app_media_track_artist(void);
```

Events: `EVT_MEDIA_STATE_CHANGED` / `EVT_MEDIA_METADATA` / `EVT_MEDIA_VOLUME_CHANGED`.

### 5.4 `app_phone` -- HFP calls

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

/* number/contact strings are owned internally by app_phone; the pointers are valid until the state changes */
const char *app_phone_peer_number(void);
const char *app_phone_peer_name(void);
```

Events: `EVT_PHONE_STATE_CHANGED` / `EVT_PHONE_RING`.

### 5.5 `app_notify` -- notification forwarding

```c
extern const app_module_t app_notify_module;

#define APP_NOTIFY_MAX 20      /* ring buffer entry count */

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

Events: `EVT_NOTIFY_NEW` / `EVT_NOTIFY_REMOVED` / `EVT_NOTIFY_CLEARED` -- the payload only carries the id; details go through `app_notify_get()`.

**Memory estimate**: one entry is 4 + 4 + 1 + 24 + 48 + 64 = 145 bytes (~152B with alignment), x20 ≈ 3KB.

### 5.6 `app_time` -- time semantics layer

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

**Not covered by the skeleton**: alarms, schedules -- to be brainstormed separately later.

### 5.7 `app_power` -- battery and charging

```c
extern const app_module_t app_power_module;

uint8_t app_power_battery_percent(void);
bool    app_power_is_charging(void);
```

Events: `EVT_POWER_LEVEL` / `EVT_POWER_LOW` / `EVT_POWER_CHARGING`.

### 5.8 `app_setting` -- thin KV shell (ultimately calls FlashDB)

```c
extern const app_module_t app_setting_module;

/* Key naming convention (written in comments, no runtime validation):
 *   dot-separated style, e.g. "user.age" / "display.brightness" / "vibrate.enable" */

int  app_setting_get_u32 (const char *key, uint32_t *out, uint32_t default_val);
int  app_setting_set_u32 (const char *key, uint32_t val);
int  app_setting_get_str (const char *key, char *out, size_t out_size, const char *default_val);
int  app_setting_set_str (const char *key, const char *val);
int  app_setting_get_blob(const char *key, void *out, size_t *inout_size);
int  app_setting_set_blob(const char *key, const void *val, size_t size);
int  app_setting_del     (const char *key);
```

Events: `EVT_SETTING_CHANGED`, payload = `app_evt_setting_changed_t { app_setting_key_id_t key; }`.

**Requirement**: `app_setting.c` maintains a `key string -> key enum` mapping table; after a `set_*` successfully writes to FlashDB, it publishes `EVT_SETTING_CHANGED` according to the mapping table. During the skeleton phase the mapping table is empty and no actual mapping is done (the enum only has `APP_SETTING_KEY_UNKNOWN`).

---

## 6. CMakeLists Integration

### 6.1 Reuse the existing generic template

`applications/port/CMakeLists.txt` is already a generic template: glob `.c/.cpp/.h`, feed them to `${APP_TARGET_NAME}`, add the current directory to the include path, and recurse with add_subdirectory. `component/CMakeLists.txt` uses the same template plus a skip list.

**This skeleton reuses the same template**:
- `app/CMakeLists.txt`: fully reuses the shape of `port/CMakeLists.txt` (only recurses into subdirectories when there are no `.c` files)
- Each module's `app/<module>/CMakeLists.txt`: the same template, taking the `SOURCES AND HEADERS` branch

**10 new CMakeLists in total** (one for `app/` plus one for each of the 9 modules), **with basically identical content**.

**No skip list** -- the app layer is all self-written modules, with no vendored third-party libraries.

### 6.2 No changes to the root CMakeLists

The `foreach(subdir ${SUBDIRS})` at the end of `applications/CMakeLists.txt` already automatically `add_subdirectory`s any subdirectory containing a `CMakeLists.txt` -- **as long as `app/` has a `CMakeLists.txt`, it takes effect automatically**.

### 6.3 include path strategy

Each module's CMakeLists adds **its own directory** to the include path. Therefore other modules write includes **flat**:

```c
#include "app_health.h"          /* correct */
```

There is no need for `#include "app_health/app_health.h"` -- this follows the project's existing style (e.g. `app_lower_init.h`, `flash_map.h`).

Includes of all submodule headers inside `app_modules.c` are also flat.

---

## 7. `main.c` Mount Point

**Only 3 lines change**, located **after** `gui_server_init()`:

```c
#include "app_core.h"     /* added */

/* ...existing code unchanged... */
gui_server_init();
gui_set_keep_active_time(10000000);

app_core_init();          /* added */
app_core_start();         /* added */

return 0;
```

**About `main()` returning**: after `main` returns, the Zephyr main thread terminates, and events continue to be driven by each module's own task and the event bus dispatcher task -- this already matches the current `main.c` behavior and needs no extra handling.

---

## 8. Skeleton Completion Verification

After the skeleton is implemented, the following should be observable:

1. **It compiles** -- no undefined symbols, no link failures
2. **Startup log** outputs in the following order:
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
   ... (the 8 modules start in order, failures are skipped)
   [app_core] EVT_SYS_READY published
   ```
3. **The system keeps running** -- `main` exits and the dispatcher task stays alive waiting for events
4. **The event bus API is callable but does not actually dispatch** -- `app_event_publish` currently returns 0 without triggering callbacks (because the dispatcher internals are still a TODO); this is an **allowed state** during the skeleton phase, and the real implementation is left to a later plan

---

## 9. Explicit Exclusions at a Glance

| Item | Status | Reason |
|---|---|---|
| `app_ota/` | Removed | User explicitly said "don't implement OTA yet"; the event id range `0x900` is kept as a placeholder |
| `app_shell/` | Removed | User rejected it; use Zephyr `SHELL_CMD_REGISTER` directly |
| `app_log/` | Removed | User rejected it; use Zephyr LOG / `DBG_DIRECT` directly |
| OSAL layer | Not built | User specified Realtek OSIF |
| Kconfig switches | Not added | YAGNI; modules are compiled in unconditionally |
| UI layer | Not involved | User handles it |
| eBadge features | Not involved | Only the hardware is reused |
| Business implementation | Not involved | Outside the skeleton spec scope; each module is brainstormed separately |
| Unit tests | Not added | There is no implementation to test during the skeleton phase |

---

## 10. Future Roadmap (not a commitment of this spec)

Each business module will later go through its own round of brainstorm -> spec -> plan -> implement; the suggested order is:

1. **Actually implement** the `app_core` event bus (it is a TODO during the skeleton phase)
2. `app_ble` private GATT services + phone App data sync protocol
3. `app_time` time sync (depends on 2)
4. Wire `app_setting` into FlashDB
5. `app_health` aggregation layer + step counter integration
6. `app_notify` ANCS + self-developed channel
7. `app_phone` HFP state machine
8. `app_media` A2DP/AVRCP
9. `app_power` battery algorithm and low-battery policy
10. Brainstorm `app_ota` separately and add it back

---

## Appendix A: Skeleton File List

Total new files: **10 CMakeLists + 8 `app_core/` files + 8x2=16 business module `.h/.c` files** = **34 files**.

Modified files: **`applications/main.c`** with only +3 lines.

No deletions.

## Appendix B: Consistency with the Project's Existing Code Style

- Header guards use `#ifndef __XXX_H__` (matching `app_lower_init.h`)
- Wrapped in `extern "C"` (matching the existing C++ mixed-build needs)
- No separate copyright block at the top of headers (matching `app_lower_init.h`)
- CMakeLists generic template (matching `port/CMakeLists.txt` / `component/CMakeLists.txt`)
- Flat include paths (matching `flash_map.h` / `app_lower_init.h`)
