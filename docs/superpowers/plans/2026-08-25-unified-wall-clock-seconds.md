# 统一墙上时钟秒 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把协议、固件、手机 App 三侧的时间标量统一为单一的墙上时钟秒（wall clock seconds，1970 纪元，手机定锚，固件永不加减偏移），根除现有的双重时区偏移缺陷。

**Architecture:** 手机把用户本地日历按 UTC 规则折算成 `uint32_t` 秒发送；固件原样写入 RTC、原样读出、原样发布事件。协议层 0x01 键从 32-bit packed 日历字段改为 4 字节大端秒。`app_time_local_t` 和所有时区偏移代码删除，`struct tm` + `gmtime_r` 接管日历展开职责。

**Tech Stack:** C11（固件，newlib + Zephyr），Dart/Flutter（手机），host gcc 宿主测试（无 Zephyr，stub posix + event bus），`cmd.exe /c "D:\\Android-dev-tool\\flutter\\bin\\flutter.bat test ..."` 运行 Dart 测试。

**Spec:** `docs/superpowers/specs/2026-08-25-unified-wall-clock-seconds-design.md`

## Global Constraints

- 断裂式变更：固件与 App **必须**同版本升级，不引入版本门。
- 固件侧禁止使用 `mktime` / `localtime_r`（依赖 `TZ`）；只允许 `gmtime_r` + 手写 `civil_to_epoch`。
- 手机侧所有 `DateTime.utc(...)` 调用必须明确传年月日时分秒六个字段，**不得**用 `DateTime(...)` 本地构造后转换。
- 纪元统一为 1970-01-01 00:00:00，不出现 `946684800`（2000 纪元）常数。
- Flash 上已有记录（`ts_utc` 字段）与新语义数值相同，**无需迁移**，仅改名。
- 宿主测试必须在 `TZ=`（unset）、`TZ=America/New_York`、`TZ=Asia/Shanghai` 三种环境下全部通过。

---

## File Structure

| 文件 | 操作 | 说明 |
|---|---|---|
| `app/app_time/app_time.h` | 修改 | 删除 `app_time_local_t`、`app_time_set_local`、`app_time_to_local`；新增 `app_time_set(uint32_t)`、`app_time_to_calendar(uint32_t, struct tm*)` |
| `app/app_time/app_time.c` | 修改 | 删除 `s_tz_min`、`local_sec_from_utc`、`app_time_to_local`、`app_time_set_local`；新增 `app_time_set`、`app_time_to_calendar`；修改 `rtc_second_cb` 去掉时区平移 |
| `app/app_core/app_event_defs.h` | 修改 | `app_evt_time_synced_t` 替换为单个 `uint32_t sec` |
| `app/app_protocol/hmi_l2_cmd_settings.h` | 修改 | 删除 `hmi_l2_decode_time` 声明，删除 `app_time.h` include |
| `app/app_protocol/hmi_l2_cmd_settings.c` | 修改 | 删除 `is_leap_year`、`days_in_month`、`hmi_l2_decode_time`；改为读 4 字节大端秒 |
| `app/app_health/app_health_internal.h` | 修改 | `ts_utc` → `ts`（字段名）；注释更新 |
| `app/app_health/health_worker.c` | 修改 | `ts_utc` → `ts`；`boundary_utc` → `boundary_sec` |
| `app/app_health/health_db.c` | 修改 | `ts_utc` → `ts`（10 处）；`health_db_save_synced_ts` 参数名 |
| `app/app_protocol/hmi_l2_cmd_sport.c` | 修改 | `r->ts_utc` → `r->ts`；`app_time_to_local` → `app_time_to_calendar`；日志格式化改 `tm_year+1900`/`tm_mon+1` |
| `component/protocol/hmi_l2.h` | 修改 | 删除 `HMI_L2_SET_ALARM`、`HMI_L2_GET_ALARM_REQ`、`HMI_L2_GET_ALARM_RSP` |
| `component/protocol/BLE_PROTOCOL_SPEC.html` | 修改 | 0x01 表格换为秒；删除 0x02/0x03/0x04 闹钟章节；0x21 纪元改 1970；上行 Timestamp 措辞更新 |
| `tests/app_time/posix_stub.h` | 新建 | 宿主测试用 posix + event bus stub 头文件 |
| `tests/app_time/posix_stub.c` | 新建 | stub 实现 |
| `tests/app_time/test_app_time.c` | 新建 | 7 个宿主单元测试 |
| `tests/app_time/Makefile` | 新建 | `make run` 在三个 TZ 下跑测试 |
| `HoneyBox/lib/services/watch_time_protocol.dart` | 修改 | packed 位运算 → 4 字节大端秒；`DateTime.utc(...)` 折算 |
| `HoneyBox/test/services/watch_time_protocol_test.dart` | 修改 | 更新断言向量 |
| `HoneyBox/lib/services/watch_health_protocol.dart` | 修改 | `_readTimestamp` 去掉 `.toLocal()` |
| `HoneyBox/test/services/watch_health_protocol_test.dart` | 修改 | 去掉 `.toLocal()` |
| `HoneyBox/lib/pages/watch/health/watch_health_data.dart` | 修改 | `_today` 及 6 处比较改为 UTC-flagged |
| `HoneyBox/test/helpers/watch_health_fixture.dart` | 修改 | timestamps 改为 UTC-flagged `DateTime.utc(...)` |

---

## Task 1: 建立宿主测试骨架（先写失败测试）

**Files:**
- Create: `tests/app_time/posix_stub.h`
- Create: `tests/app_time/posix_stub.c`
- Create: `tests/app_time/test_app_time.c`
- Create: `tests/app_time/Makefile`

**Interfaces:**
- Consumes: `app/app_time/app_time.h`（当前版本），`app/app_core/app_event_defs.h`
- Produces: `stub_set_rtc()`、`stub_tick()`、`stub_events_reset()`、`stub_event_count_of()`、`stub_event_sec_of()` 供 Task 2 测试使用

- [ ] **Step 1: 写 posix_stub.h**

```c
/* tests/app_time/posix_stub.h */
#ifndef POSIX_STUB_H
#define POSIX_STUB_H

#include "posix.h"
#include "ioctls/posix_ioctl_rtc.h"
#include "app_event.h"
#include <stdint.h>
#include <stddef.h>

extern posix_rtc_time_t stub_rtc;
extern void (*stub_tick_cb)(posix_fd_t fd, void *arg);

#define STUB_MAX_EVENTS 16u
typedef struct { app_event_id_t id; uint32_t sec; size_t len; } stub_event_t;
extern stub_event_t stub_events[STUB_MAX_EVENTS];
extern unsigned     stub_event_count;

void     stub_events_reset(void);
unsigned stub_event_count_of(app_event_id_t id);
uint32_t stub_event_sec_of(app_event_id_t id);
void     stub_set_rtc(uint16_t year, uint8_t mon, uint8_t mday,
                      uint8_t hour, uint8_t min, uint8_t sec);
void     stub_tick(void);

#endif
```

- [ ] **Step 2: 写 posix_stub.c**

```c
/* tests/app_time/posix_stub.c */
#include "posix_stub.h"
#include <string.h>

posix_rtc_time_t stub_rtc;
void (*stub_tick_cb)(posix_fd_t fd, void *arg);
stub_event_t stub_events[STUB_MAX_EVENTS];
unsigned     stub_event_count;

int posix_port_init_all(void) { return 0; }
posix_fd_t posix_open(const char *p) { (void)p; return (posix_fd_t)1; }
int posix_close(posix_fd_t fd)       { (void)fd; return POSIX_OK; }

int posix_ioctl(posix_fd_t fd, unsigned long cmd, void *arg)
{
    (void)fd;
    switch (cmd) {
    case POSIX_RTC_IOCTL_GET_TIME:
        *(posix_rtc_time_t *)arg = stub_rtc; return POSIX_OK;
    case POSIX_RTC_IOCTL_SET_TIME:
        stub_rtc = *(posix_rtc_time_t *)arg; return POSIX_OK;
    case POSIX_RTC_IOCTL_SET_UPDATE_CB:
        stub_tick_cb = ((posix_rtc_update_t *)arg)->callback; return POSIX_OK;
    default: return POSIX_ERR_NOSUPP;
    }
}

int app_event_subscribe(app_event_id_t id, app_event_cb_t cb, void *u)
    { (void)id;(void)cb;(void)u; return 0; }
int app_event_publish(app_event_id_t id, const void *p, size_t l)
    { return app_event_publish_isr(id, p, l); }
int app_event_publish_isr(app_event_id_t id, const void *p, size_t l)
{
    if (stub_event_count >= STUB_MAX_EVENTS) return -1;
    stub_event_t *e = &stub_events[stub_event_count++];
    e->id = id; e->len = l;
    e->sec = (l == sizeof(uint32_t) && p) ? *(const uint32_t *)p : 0u;
    return 0;
}

void stub_events_reset(void)
    { memset(stub_events, 0, sizeof(stub_events)); stub_event_count = 0u; }
unsigned stub_event_count_of(app_event_id_t id)
    { unsigned n=0; for(unsigned i=0;i<stub_event_count;i++) if(stub_events[i].id==id)n++; return n; }
uint32_t stub_event_sec_of(app_event_id_t id)
    { for(unsigned i=0;i<stub_event_count;i++) if(stub_events[i].id==id) return stub_events[i].sec; return 0u; }

void stub_set_rtc(uint16_t year, uint8_t mon, uint8_t mday,
                  uint8_t hour, uint8_t min, uint8_t sec)
{
    stub_rtc.year=year; stub_rtc.month=mon; stub_rtc.mday=mday;
    stub_rtc.hour=hour; stub_rtc.minute=min; stub_rtc.second=sec;
    stub_rtc.wday=0xFFu; stub_rtc.yday=0xFFFFu; stub_rtc.nsec=0u;
}
void stub_tick(void)
    { if (stub_tick_cb) stub_tick_cb((posix_fd_t)1, NULL); }
```

- [ ] **Step 3: 写 test_app_time.c（7 个测试，全部引用新 API）**

```c
/* tests/app_time/test_app_time.c */
#include "app_time.h"
#include "app_event_defs.h"
#include "posix_stub.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static unsigned g_fail;
#define CHECK(cond, ...) \
    do { if(!(cond)){ g_fail++; printf("  FAIL %s:%d: ",__FILE__,__LINE__); printf(__VA_ARGS__); printf("\n"); } } while(0)

/* 2026-08-25 15:30:00 UTC-fold => 1787671800 */
#define WCS_1530  1787671800u
/* 2026-08-25 00:00:00 UTC-fold => 1787616000 */
#define WCS_0000  1787616000u

static void test_now_is_unshifted(void) {
    stub_set_rtc(2026,8,25,15,30,0);
    uint32_t s = app_time_now();
    CHECK(s == WCS_1530, "app_time_now=%u want %u", (unsigned)s, WCS_1530);
}

static void test_set_roundtrips_and_fills_wday(void) {
    memset(&stub_rtc,0,sizeof(stub_rtc));
    int rc = app_time_set(WCS_1530);
    CHECK(rc == 0, "app_time_set rc=%d", rc);
    CHECK(stub_rtc.year==2026 && stub_rtc.month==8 && stub_rtc.mday==25,
          "date %u-%u-%u",(unsigned)stub_rtc.year,(unsigned)stub_rtc.month,(unsigned)stub_rtc.mday);
    CHECK(stub_rtc.hour==15 && stub_rtc.minute==30 && stub_rtc.second==0,
          "time %u:%u:%u",(unsigned)stub_rtc.hour,(unsigned)stub_rtc.minute,(unsigned)stub_rtc.second);
    /* 2026-08-25 is Tuesday -> wday 2 */
    CHECK(stub_rtc.wday==2, "wday=%u want 2 (Tuesday)",(unsigned)stub_rtc.wday);
    CHECK(app_time_now() == WCS_1530, "set/now not identity");
}

static void test_to_calendar(void) {
    struct tm t; memset(&t,0,sizeof(t));
    app_time_to_calendar(WCS_1530, &t);
    CHECK(t.tm_year+1900==2026,"year=%d",t.tm_year+1900);
    CHECK(t.tm_mon+1==8,"mon=%d",t.tm_mon+1);
    CHECK(t.tm_mday==25,"mday=%d",t.tm_mday);
    CHECK(t.tm_hour==15,"hour=%d",t.tm_hour);
    CHECK(t.tm_min==30,"min=%d",t.tm_min);
    CHECK(t.tm_wday==2,"wday=%d",t.tm_wday);
}

static void test_day_changed_at_midnight(void) {
    stub_set_rtc(2026,8,24,23,59,59);
    stub_events_reset(); stub_tick();
    CHECK(stub_event_count_of(EVT_TIME_DAY_CHANGED)==0,"day event on first tick");

    stub_set_rtc(2026,8,25,0,0,0);
    stub_events_reset(); stub_tick();
    CHECK(stub_event_count_of(EVT_TIME_DAY_CHANGED)==1,"no DAY_CHANGED at midnight");
    CHECK(stub_event_sec_of(EVT_TIME_DAY_CHANGED)==WCS_0000,
          "DAY_CHANGED sec=%u want %u",(unsigned)stub_event_sec_of(EVT_TIME_DAY_CHANGED),WCS_0000);
}

static void test_no_day_change_at_1600(void) {
    stub_set_rtc(2026,8,25,15,59,59);
    stub_events_reset(); stub_tick();   /* adopt day */
    stub_set_rtc(2026,8,25,16,0,0);
    stub_events_reset(); stub_tick();
    CHECK(stub_event_count_of(EVT_TIME_DAY_CHANGED)==0,"DAY_CHANGED at 16:00 => tz leak");
    CHECK(stub_event_count_of(EVT_TIME_TICK_15MIN)==1,"16:00 not a quarter-hour boundary");
}

static void test_quarter_hour_boundaries(void) {
    stub_set_rtc(2026,8,25,10,15,0);
    stub_events_reset(); stub_tick();
    CHECK(stub_event_count_of(EVT_TIME_TICK_15MIN)==1,"10:15 not a boundary");
    stub_set_rtc(2026,8,25,10,16,0);
    stub_events_reset(); stub_tick();
    CHECK(stub_event_count_of(EVT_TIME_TICK_15MIN)==0,"10:16 fired a tick");
}

static void test_unset_clock_is_silent(void) {
    stub_set_rtc(1969,1,1,0,0,0);
    stub_events_reset(); stub_tick();
    CHECK(stub_event_count==0,"events with clock unset");
}

static void test_null_to_calendar_is_noop(void) {
    /* must not crash */
    app_time_to_calendar(WCS_1530, NULL);
    CHECK(1, "");   /* reaching here is the pass */
}

int main(void) {
    const char *tz = getenv("TZ");
    printf("app_time tests (TZ=%s)\n", (tz && *tz) ? tz : "<unset>");
    stub_set_rtc(2026,8,25,15,30,0);
    int rc = app_time_module.init();
    CHECK(rc==0,"init rc=%d",rc);
    CHECK(stub_tick_cb!=NULL,"1Hz cb not bound");

    test_now_is_unshifted();
    test_set_roundtrips_and_fills_wday();
    test_to_calendar();
    test_day_changed_at_midnight();
    test_no_day_change_at_1600();
    test_quarter_hour_boundaries();
    test_unset_clock_is_silent();
    test_null_to_calendar_is_noop();

    if (g_fail==0) { printf("  all passed\n"); return 0; }
    printf("  %u check(s) failed\n", g_fail);
    return 1;
}
```

- [ ] **Step 4: 写 Makefile**

```makefile
# tests/app_time/Makefile
APP := ../..
CFLAGS := -std=gnu11 -Wall -Wextra -Werror -g \
          -I$(APP)/component/posix/include \
          -I$(APP)/component/posix/examples/port \
          -I$(APP)/app/app_core \
          -I$(APP)/app/app_time \
          -I.

run: test_app_time
	@TZ= ./test_app_time
	@TZ=America/New_York ./test_app_time
	@TZ=Asia/Shanghai ./test_app_time

test_app_time: test_app_time.c posix_stub.c $(APP)/app/app_time/app_time.c posix_stub.h
	$(CC) $(CFLAGS) -o $@ test_app_time.c posix_stub.c $(APP)/app/app_time/app_time.c

clean:
	rm -f test_app_time

.PHONY: run clean
```

- [ ] **Step 5: 运行，确认编译失败（新 API 尚不存在）**

```bash
cd tests/app_time && make run
```

期望：编译报错 `implicit declaration of function 'app_time_set'` 和 `app_time_to_calendar`。

- [ ] **Step 6: Commit 测试骨架**

```bash
git add tests/app_time/
git commit -m "test(app_time): add host unit test harness (failing – new API not yet impl)"
```

---

## Task 2: 修改 app_time.h / app_time.c

**Files:**
- Modify: `app/app_time/app_time.h`
- Modify: `app/app_time/app_time.c`

**Interfaces:**
- Produces: `app_time_set(uint32_t sec)` → `int`；`app_time_to_calendar(uint32_t sec, struct tm *out)` → `void`
- Removes: `app_time_local_t`，`app_time_set_local`，`app_time_to_local`，`s_tz_min`，`local_sec_from_utc`
- Keeps: `app_time_now()`，`civil_to_epoch()`（static，供 `app_time_now` 读 RTC）

- [ ] **Step 1: 替换 app_time.h**

完整新内容：

```c
#ifndef __APP_TIME_H__
#define __APP_TIME_H__

#include "app_module.h"
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file  app_time.h
 * @brief Wall-clock semantics on top of the RTC driver.
 *
 * Sole writer of the hardware RTC. Publishes EVT_TIME_TICK_15MIN and
 * EVT_TIME_DAY_CHANGED so consumers never re-derive them.
 *
 * The scalar: uint32_t wall clock seconds from 1970-01-01 00:00:00, whose
 * value IS the moment the watch face shows. The phone defines the anchor;
 * this module never adds or subtracts an offset. gmtime_r() is the correct
 * inverse -- not localtime_r/mktime, which consult TZ.
 */
extern const app_module_t app_time_module;

#define APP_TIME_TICK_MIN_STEP  15u

/** Wall clock seconds; 0 if the RTC is unset or unreadable. */
uint32_t app_time_now(void);

/**
 * Store wall clock seconds in the RTC. Uses gmtime_r to expand the scalar
 * into calendar fields -- wday is computed by libc, not guessed.
 * Returns 0 on success, negative on failure.
 */
int app_time_set(uint32_t sec);

/**
 * Expand wall clock seconds into struct tm calendar fields.
 * Caller interprets tm_year+1900, tm_mon+1 per standard struct tm.
 * No-op when out is NULL.
 */
void app_time_to_calendar(uint32_t sec, struct tm *out);

#ifdef __cplusplus
}
#endif

#endif /* __APP_TIME_H__ */
```

- [ ] **Step 2: 替换 app_time.c**

完整新内容（关键变更点注释如下，完整文件见 Spec §5.1）：

```c
/*
 * app_time.c — wall clock ownership + boundary ticks.
 *
 * One uint32_t, from 1970-01-01 00:00:00, value = what the watch face shows.
 * The phone produces it; nothing here shifts it. No s_tz_min, no
 * local_sec_from_utc — which is exactly why the day no longer rolls at 16:00.
 */

#include "app_time.h"
#include "app_event.h"
#include "app_event_defs.h"
#include "app_log.h"
#include "posix.h"
#include "posix_port.h"
#include "ioctls/posix_ioctl_rtc.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

APP_LOG_MODULE_REGISTER(app_time);

#define APP_TIME_TICK_STEP_SEC  (APP_TIME_TICK_MIN_STEP * 60u)
#define APP_TIME_RTC_PATH       "/dev/rtc0"

static posix_fd_t s_rtc_fd = POSIX_FD_NULL;
static uint32_t   s_last_day_published;
static bool       s_day_valid;

int app_time_set(uint32_t sec)
{
    time_t    when = (time_t)sec;
    struct tm tm_buf;

    if (gmtime_r(&when, &tm_buf) == NULL) {
        APP_LOGE("gmtime_r failed sec=%u", (unsigned)sec);
        return -1;
    }

    (void)posix_port_init_all();
    posix_fd_t rtc = posix_open(APP_TIME_RTC_PATH);
    if (rtc == POSIX_FD_NULL) {
        APP_LOGE("open %s failed", APP_TIME_RTC_PATH);
        return -1;
    }

    posix_rtc_time_t rtc_time = {
        .year   = (uint16_t)(tm_buf.tm_year + 1900),
        .month  = (uint8_t)(tm_buf.tm_mon  + 1),
        .mday   = (uint8_t) tm_buf.tm_mday,
        .hour   = (uint8_t) tm_buf.tm_hour,
        .minute = (uint8_t) tm_buf.tm_min,
        .second = (uint8_t) tm_buf.tm_sec,
        .wday   = (uint8_t) tm_buf.tm_wday,  /* libc computes, never lied */
        .yday   = 0xFFFFu,
        .nsec   = 0u,
    };
    int rc = posix_ioctl(rtc, POSIX_RTC_IOCTL_SET_TIME, &rtc_time);
    posix_close(rtc);
    if (rc != POSIX_OK) {
        APP_LOGE("RTC SET_TIME failed rc=%d", rc);
        return -1;
    }

    APP_LOGI("wall clock set %04u-%02u-%02u %02u:%02u:%02u (sec=%u)",
             (unsigned)rtc_time.year, (unsigned)rtc_time.month,
             (unsigned)rtc_time.mday, (unsigned)rtc_time.hour,
             (unsigned)rtc_time.minute, (unsigned)rtc_time.second,
             (unsigned)sec);
    return 0;
}

/* Howard Hinnant algorithm. Kept because newlib has no timegm() and mktime()
 * would reintroduce TZ. This path is read-only (RTC -> scalar). */
static uint32_t civil_to_epoch(const posix_rtc_time_t *t)
{
    if (t->year < 1970 || t->month < 1 || t->month > 12 ||
        t->mday < 1 || t->mday > 31 || t->hour > 23 ||
        t->minute > 59 || t->second > 60)
        return 0;

    int32_t  year = (int32_t)t->year - (t->month <= 2 ? 1 : 0);
    int32_t  era  = year / 400;
    uint32_t yoe  = (uint32_t)(year - era * 400);
    uint32_t m    = t->month + (t->month > 2 ? -3u : 9u);
    uint32_t doy  = (153u * m + 2u) / 5u + t->mday - 1u;
    uint32_t doe  = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    int64_t  days = (int64_t)era * 146097 + (int64_t)doe - 719468;

    return (uint32_t)(days * 86400 +
                      (int64_t)t->hour   * 3600 +
                      (int64_t)t->minute * 60 +
                      (int64_t)t->second);
}

uint32_t app_time_now(void)
{
    posix_fd_t rtc = posix_open(APP_TIME_RTC_PATH);
    if (rtc == POSIX_FD_NULL) return 0;

    posix_rtc_time_t t;
    int rc = posix_ioctl(rtc, POSIX_RTC_IOCTL_GET_TIME, &t);
    posix_close(rtc);
    return (rc != POSIX_OK) ? 0 : civil_to_epoch(&t);
}

void app_time_to_calendar(uint32_t sec, struct tm *out)
{
    if (out == NULL) return;
    time_t    when = (time_t)sec;
    struct tm tm_buf;
    if (gmtime_r(&when, &tm_buf) != NULL) *out = tm_buf;
}

static uint32_t rtc_now_from_isr(void)
{
    posix_rtc_time_t t;
    if (s_rtc_fd == POSIX_FD_NULL) return 0u;
    if (posix_ioctl(s_rtc_fd, POSIX_RTC_IOCTL_GET_TIME, &t) != POSIX_OK) return 0u;
    return civil_to_epoch(&t);
}

static void rtc_second_cb(posix_fd_t fd, void *arg)
{
    (void)fd; (void)arg;

    uint32_t now = rtc_now_from_isr();
    if (now == 0u) return;

    /* Pure division: no timezone offset. Day boundary IS calendar midnight. */
    uint32_t day = now / 86400u;

    bool day_rolled = false;
    if (!s_day_valid) {
        s_last_day_published = day;
        s_day_valid = true;
    } else if (day != s_last_day_published) {
        s_last_day_published = day;
        day_rolled = true;
    }

    if (!day_rolled && (now % APP_TIME_TICK_STEP_SEC) != 0u) return;

    if (day_rolled)
        (void)app_event_publish_isr(EVT_TIME_DAY_CHANGED, &now, sizeof now);

    if ((now % APP_TIME_TICK_STEP_SEC) == 0u)
        (void)app_event_publish_isr(EVT_TIME_TICK_15MIN, &now, sizeof now);
}

static int tick_start(void)
{
    (void)posix_port_init_all();
    if (s_rtc_fd == POSIX_FD_NULL) {
        s_rtc_fd = posix_open(APP_TIME_RTC_PATH);
        if (s_rtc_fd == POSIX_FD_NULL) {
            APP_LOGE("open %s failed; boundary events disabled", APP_TIME_RTC_PATH);
            return -1;
        }
    }
    posix_rtc_update_t u = { .callback = rtc_second_cb, .arg = NULL };
    int rc = posix_ioctl(s_rtc_fd, POSIX_RTC_IOCTL_SET_UPDATE_CB, &u);
    if (rc != POSIX_OK) {
        APP_LOGE("RTC SET_UPDATE_CB failed rc=%d; boundary events disabled", rc);
        return -1;
    }
    return 0;
}

static void on_evt_time_synced(app_event_id_t id, const void *payload,
                               size_t len, void *user)
{
    (void)id; (void)payload; (void)len; (void)user;
    s_day_valid = false;
    APP_LOGI("clock synced; day tracking re-baselined");
}

static int time_init(void)
{
    if (app_event_subscribe(EVT_TIME_SYNCED, on_evt_time_synced, NULL) != 0) {
        APP_LOGE("subscribe EVT_TIME_SYNCED failed");
        return -1;
    }
    if (tick_start() != 0) return 0;   /* stay loaded without boundary events */

    APP_LOGI("app_time init: wall clock seconds, %u-min boundary ticks off RTC 1Hz",
             (unsigned)APP_TIME_TICK_MIN_STEP);
    return 0;
}

const app_module_t app_time_module = { .name = "time", .init = time_init };
```

- [ ] **Step 3: 运行测试，确认全部通过**

```bash
cd tests/app_time && make run
```

期望（三次）：`all passed`

- [ ] **Step 4: Commit**

```bash
git add app/app_time/app_time.h app/app_time/app_time.c tests/app_time/
git commit -m "feat(app_time): unified wall clock seconds — delete tz offset, add app_time_set/to_calendar"
```

---

## Task 3: 修改 app_event_defs.h — app_evt_time_synced_t

**Files:**
- Modify: `app/app_core/app_event_defs.h:125-144`

**Interfaces:**
- Removes: `app_evt_time_synced_t` 的 6 个日历字段
- Produces: `typedef struct { uint32_t sec; } app_evt_time_synced_t;`

- [ ] **Step 1: 定位并替换结构体定义**

将 `app_event_defs.h` 第 125-144 行（`app_evt_time_synced_t` 注释 + struct）替换为：

```c
/**
 * @brief  New wall clock value published on EVT_TIME_SYNCED.
 *
 * sec is wall clock seconds (1970 epoch, §3.1 of the design doc).
 * app_time is the sole subscriber that writes the RTC. Other modules
 * may listen to snap their state to the new wall clock.
 */
typedef struct
{
    uint32_t sec;
} app_evt_time_synced_t;
```

同时删除 `app_event_defs.h` 第 36-44 行 Time 区域注释中的 `UTC + timezone offset` 措辞，改为：

```c
    /* Time (0x300~)
     *
     * Tick events fire on wall-clock boundaries -- the scalar is
     * wall clock seconds (1970 epoch), which IS the moment the watch
     * face shows. No timezone offset exists anywhere in this path.
     */
```

- [ ] **Step 2: 确认 on_evt_time_synced 调用者不崩**

```bash
grep -rn "app_evt_time_synced_t\|\.year\b\|\.month\b\|\.day\b\|\.hour\b\|\.min\b\|\.sec\b" \
     app/app_health/app_health.c app/app_time/app_time.c app/app_protocol/hmi_l2_cmd_settings.c
```

`app_health.c` 的 `on_evt_time_synced` 已有 `(void)payload`，不访问字段，无需改。
`app_time.c` 的 `on_evt_time_synced` 同样忽略 payload。
只有 `hmi_l2_cmd_settings.c` 填充该结构体，在 Task 4 处理。

- [ ] **Step 3: Commit**

```bash
git add app/app_core/app_event_defs.h
git commit -m "refactor(event): simplify app_evt_time_synced_t to single uint32_t sec"
```

---

## Task 4: 修改 hmi_l2_cmd_settings.h / .c

**Files:**
- Modify: `app/app_protocol/hmi_l2_cmd_settings.h`
- Modify: `app/app_protocol/hmi_l2_cmd_settings.c`

**Interfaces:**
- Removes: `hmi_l2_decode_time`，`is_leap_year`，`days_in_month`
- Consumes: `app_time_set(uint32_t)`（Task 2），`app_evt_time_synced_t.sec`（Task 3）

- [ ] **Step 1: 替换 hmi_l2_cmd_settings.h**

```c
#ifndef _HMI_L2_CMD_SETTINGS_H_
#define _HMI_L2_CMD_SETTINGS_H_

#ifdef __cplusplus
extern "C" {
#endif

void hmi_l2_settings_register(void);

#ifdef __cplusplus
}
#endif

#endif /* _HMI_L2_CMD_SETTINGS_H_ */
```

（删除 `app_time.h` include，删除 `hmi_l2_decode_time` 声明）

- [ ] **Step 2: 替换 hmi_l2_cmd_settings.c**

```c
#include "hmi_l2_cmd_settings.h"
#include "hmi_l2.h"
#include "proto_log.h"
#include "app_time.h"
#include "app_event.h"
#include "app_event_defs.h"

static void on_cmd_settings(const hmi_l2_kv_t *kvs, uint8_t n)
{
    for (uint8_t i = 0; i < n; i++) {
        PROTO_LOG("L2 SETTINGS key=0x%02x val_len=%u",
                  kvs[i].key, (unsigned)kvs[i].val_len);

        if (kvs[i].key != HMI_L2_SET_TIME) continue;
        if (kvs[i].val_len != 4u) {
            PROTO_LOG("L2 SETTINGS invalid time payload len=%u", (unsigned)kvs[i].val_len);
            continue;
        }

        /* 4 bytes big-endian wall clock seconds, 1970 epoch. */
        const uint8_t *v = kvs[i].val;
        uint32_t sec = ((uint32_t)v[0] << 24) |
                       ((uint32_t)v[1] << 16) |
                       ((uint32_t)v[2] <<  8) |
                        (uint32_t)v[3];

        if (app_time_set(sec) != 0) {
            PROTO_LOG("L2 SETTINGS failed to set RTC");
            continue;
        }

        app_evt_time_synced_t ev = { .sec = sec };
        if (app_event_publish(EVT_TIME_SYNCED, &ev, sizeof ev) != 0)
            PROTO_LOG("L2 SETTINGS failed to publish EVT_TIME_SYNCED");
    }
}

void hmi_l2_settings_register(void)
{
    hmi_l2_register(HMI_L2_CMD_SETTINGS, on_cmd_settings);
}
```

- [ ] **Step 3: west build 确认编译通过**

```bash
west build -b rtl87x3g_evb
```

- [ ] **Step 4: Commit**

```bash
git add app/app_protocol/hmi_l2_cmd_settings.h app/app_protocol/hmi_l2_cmd_settings.c
git commit -m "feat(settings): decode 0x01 as 4-byte big-endian wall clock seconds"
```

---

## Task 5: 修改 hmi_l2.h — 删除闹钟宏

**Files:**
- Modify: `component/protocol/hmi_l2.h:29-32`

**Interfaces:**
- Removes: `HMI_L2_SET_ALARM 0x02u`，`HMI_L2_GET_ALARM_REQ 0x03u`，`HMI_L2_GET_ALARM_RSP 0x04u`（无引用点，安全删除）

- [ ] **Step 1: 确认无引用**

```bash
grep -rn "HMI_L2_SET_ALARM\|HMI_L2_GET_ALARM" --include=*.c --include=*.h . | grep -v build
```

期望：零结果。

- [ ] **Step 2: 删除三行宏定义**

将 `hmi_l2.h` 中：
```c
#define HMI_L2_SET_ALARM        0x02u
#define HMI_L2_GET_ALARM_REQ    0x03u
#define HMI_L2_GET_ALARM_RSP    0x04u
```
三行删除（`HMI_L2_SET_TIME 0x01u` 行保留，下面直接接 `HMI_L2_SET_STEP_TARGET 0x05u`）。

- [ ] **Step 3: west build 确认无编译错误**

```bash
west build -b rtl87x3g_evb
```

- [ ] **Step 4: Commit**

```bash
git add component/protocol/hmi_l2.h
git commit -m "refactor(protocol): remove unimplemented alarm key macros (0x02/0x03/0x04)"
```

---

## Task 6: 修改 health 层 — ts_utc → ts 改名

**Files:**
- Modify: `app/app_health/app_health_internal.h:63`
- Modify: `app/app_health/health_worker.c`（`ts_utc`、`boundary_utc`）
- Modify: `app/app_health/health_db.c`（`ts_utc` 共 10 处，`health_db_save_synced_ts` 参数名）

**Interfaces:**
- `health_pedo_record_t.ts_utc` → `.ts`（类型、偏移、布局不变，Flash 记录不受影响）
- `health_worker_on_bucket_boundary(uint32_t boundary_utc)` → `(uint32_t boundary_sec)`

- [ ] **Step 1: 修改 app_health_internal.h**

将第 63 行：
```c
    uint32_t ts_utc;        /* UTC epoch seconds captured at flush moment  */
```
改为：
```c
    uint32_t ts;            /* wall clock seconds at flush moment           */
```

将第 108 行注释 `using the record's ts_utc as` 改为 `using the record's ts as`。

将第 150-156 行注释和函数签名：
```c
/* Bucket boundary reached: write the closed bucket to the TSDB. @c boundary_utc
 * ...
void health_worker_on_bucket_boundary(uint32_t boundary_utc);
```
改为：
```c
/* Bucket boundary reached: write the closed bucket to the TSDB. @c boundary_sec
 * is the boundary instant in wall clock seconds and becomes the record's
 * timestamp. A no-op when no worker is running. */
void health_worker_on_bucket_boundary(uint32_t boundary_sec);
```

- [ ] **Step 2: 修改 health_worker.c**

全文替换 `ts_utc` → `ts`（影响第 280 行初始化器 `.ts_utc = ts`）。  
全文替换 `boundary_utc` → `boundary_sec`（影响第 222、236、269、328、330、337 行）。  
第 222 行注释 `@c boundary_utc` → `@c boundary_sec`。

- [ ] **Step 3: 修改 health_db.c**

全文替换 `ts_utc` → `ts`，共 10 处：
- 第 8、13 行注释
- 第 92、96、99、101、105、159（参数名）、170、224、267、291、378、380 行代码

`health_db_save_synced_ts` 函数参数 `uint32_t ts_utc` → `uint32_t ts`，函数体内 `&ts_utc` → `&ts`。

- [ ] **Step 4: west build**

```bash
west build -b rtl87x3g_evb
```

- [ ] **Step 5: Commit**

```bash
git add app/app_health/app_health_internal.h app/app_health/health_worker.c app/app_health/health_db.c
git commit -m "refactor(health): rename ts_utc->ts and boundary_utc->boundary_sec (pure rename, no logic change)"
```

---

## Task 7: 修改 hmi_l2_cmd_sport.c — 适配新 API

**Files:**
- Modify: `app/app_protocol/hmi_l2_cmd_sport.c:102-105,185-193`

**Interfaces:**
- `r->ts_utc` → `r->ts`（Task 6 改名后）
- `app_time_to_local(...)` → `app_time_to_calendar(...)`（Task 2）
- 日志格式化：`lt.year` → `tm.tm_year+1900`，`lt.month` → `tm.tm_mon+1`

- [ ] **Step 1: 修改 sport_encode_record 字段名**

第 102-105 行，将 `r->ts_utc` 改为 `r->ts`（4 处）：

```c
    out[p++] = (uint8_t)(r->ts >> 24);
    out[p++] = (uint8_t)(r->ts >> 16);
    out[p++] = (uint8_t)(r->ts >>  8);
    out[p++] = (uint8_t)(r->ts);
```

- [ ] **Step 2: 修改日志调用**

将第 185-193 行：
```c
        app_time_local_t lt;
        app_time_to_local(rec.ts_utc, &lt);
        PROTO_LOG("L2 SPORT rec %u: %04u-%02u-%02u %02u:%02u steps=%u",
                  (unsigned)n, (unsigned)lt.year, (unsigned)lt.month,
                  (unsigned)lt.day, (unsigned)lt.hour, (unsigned)lt.min,
                  (unsigned)rec.steps);
```
改为：
```c
        struct tm lt;
        app_time_to_calendar(rec.ts, &lt);
        PROTO_LOG("L2 SPORT rec %u: %04u-%02u-%02u %02u:%02u steps=%u",
                  (unsigned)n,
                  (unsigned)(lt.tm_year + 1900), (unsigned)(lt.tm_mon + 1),
                  (unsigned)lt.tm_mday, (unsigned)lt.tm_hour,
                  (unsigned)lt.tm_min, (unsigned)rec.steps);
```

在文件顶部 includes 中加 `#include <time.h>`（如果尚未有）。

- [ ] **Step 3: west build**

```bash
west build -b rtl87x3g_evb
```

- [ ] **Step 4: Commit**

```bash
git add app/app_protocol/hmi_l2_cmd_sport.c
git commit -m "refactor(sport): adapt to renamed ts field and app_time_to_calendar"
```

---

## Task 8: 修改 BLE_PROTOCOL_SPEC.html

**Files:**
- Modify: `component/protocol/BLE_PROTOCOL_SPEC.html`

注：该文件在 `component/protocol/` 子仓库，需在子仓库单独 commit。

- [ ] **Step 1: 替换 0x01 时间设置表格**

定位第 711 行 `<h4 id="0x01-时间设置">`。  
将其后的整个 `<table>` 块（6 行 Year/Month/Day/Hour/Minute/Second）替换为：

```html
<h4 id="0x01-时间设置">0x01 — 时间设置</h4>
<p><strong>方向</strong>：手机 → 设备（每次绑定成功后需同步）<br />
<strong>Value（4 bytes，Big-Endian）</strong>：墙上时钟秒（wall clock seconds），自 1970-01-01 00:00:00 起算。</p>
<table>
<thead><tr><th>字节序</th><th>宽度</th><th>说明</th></tr></thead>
<tbody>
<tr><td>大端</td><td>4 bytes</td><td>其值为设备应当显示的时刻。手机负责把用户本地日历折算为秒；<strong>设备不做任何时区处理</strong>。有效范围 1970-01-01 至 2106-02-07。</td></tr>
</tbody>
</table>
<p><strong>不兼容变更</strong>（2026-08-25）：原格式为 32-bit packed 日历字段（Year 6 bits + Month 4 bits + Day 5 bits + Hour 5 bits + Minute 6 bits + Second 6 bits），现已废弃。固件与 App 必须同版本升级。</p>
```

- [ ] **Step 2: 删除闹钟 TOC 三行**

在 TOC 表格（约第 660-680 行）中，删除：
```html
<tr>
<td><code>0x02</code></td>
<td>闹钟设置</td>
</tr>
<tr>
<td><code>0x03</code></td>
<td>获取设备闹钟列表请求</td>
</tr>
<tr>
<td><code>0x04</code></td>
<td>获取设备闹钟列表返回</td>
</tr>
```

- [ ] **Step 3: 删除闹钟章节主体**

删除第 754-812 行（从 `<h4 id="0x02-0x04-闹钟设置-获取闹钟列表返回">` 到 `<h4 id="0x03-获取设备闹钟列表请求">` 节末尾 `<p><strong>Value</strong>：空</p>` 为止，共两个小节）。

- [ ] **Step 4: 更新 0x21 Timestamp 纪元**

定位第 1634 行：
```html
<td>从 2000 年起的秒数</td>
```
改为：
```html
<td>墙上时钟秒（自 1970-01-01 起算，与 §0x01 定义一致）</td>
```

- [ ] **Step 5: 更新上行 Timestamp 措辞**

定位第 1178-1181 行（sport 桶说明段落）：
```html
<p>手机不得把 Timestamp 当作桶开始时间，也不得再对 Timestamp 做时区、
Date/Offset 或 15 分钟前移转换。</p>
```
改为：
```html
<p>Timestamp 即设备显示的墙上时钟时刻（wall clock seconds，1970 纪元），
手机直接渲染即可，无需任何时区转换。</p>
```

定位第 1224 行：
```html
<td>Unix 时间戳（秒），与 FlashDB 记录时间一致</td>
```
改为：
```html
<td>墙上时钟秒（自 1970-01-01 起算，与设备显示一致），与 FlashDB 记录时间一致</td>
```

定位第 1357 行（sleep record）：
```html
<td>状态实际生效的 Unix 时间戳（秒），与 FlashDB 记录时间一致</td>
```
改为：
```html
<td>状态实际生效的墙上时钟秒（自 1970-01-01 起算），与 FlashDB 记录时间一致</td>
```

- [ ] **Step 6: Commit（在 component/protocol 子仓库）**

```bash
cd component/protocol
git add BLE_PROTOCOL_SPEC.html
git commit -m "docs: unify all timestamps to wall clock seconds (1970 epoch, breaking change)"
cd ../..
```

---

## Task 9: 修改手机 watch_time_protocol.dart

**Files:**
- Modify: `C:\Users\howie_wang.RSDOMAIN\workspace\HoneyBox\lib\services\watch_time_protocol.dart`
- Modify: `C:\Users\howie_wang.RSDOMAIN\workspace\HoneyBox\test\services\watch_time_protocol_test.dart`

WSL 路径：`/mnt/c/Users/howie_wang.RSDOMAIN/workspace/HoneyBox/...`

- [ ] **Step 1: 替换 watch_time_protocol.dart**

```dart
import 'dart:typed_data';
import 'ble_cmd_registry.dart';

/// Watch time sync (CMD 0x02) frame builder.
///
/// Value is 4 bytes big-endian wall clock seconds (1970 epoch).
/// The caller passes the user's local DateTime; this class folds it with
/// UTC rules via DateTime.utc() -- which is the strict inverse of gmtime_r
/// on the firmware side.
class WatchTimeProtocol {
  WatchTimeProtocol._();

  static const int minimumYear = 1970;
  static const int maximumYear = 2106;

  static Uint8List buildSetTime(DateTime localTime) {
    if (localTime.year < minimumYear || localTime.year > maximumYear) {
      throw ArgumentError.value(
        localTime.year,
        'localTime.year',
        'Watch time year must be between $minimumYear and $maximumYear',
      );
    }

    // Fold the local calendar fields with UTC rules.
    // DateTime.utc(...) treats the supplied fields as UTC, which matches
    // exactly how gmtime_r unfolds the scalar on the firmware side.
    // DO NOT use DateTime(localTime.year, ...) here -- that would apply
    // the device timezone a second time.
    final seconds = DateTime.utc(
      localTime.year,
      localTime.month,
      localTime.day,
      localTime.hour,
      localTime.minute,
      localTime.second,
    ).millisecondsSinceEpoch ~/ 1000;

    return Uint8List.fromList([
      BleCmd.watchTime,
      0x00,
      BleCmdWatchTimeKey.setTime,
      0x00,
      0x04,
      (seconds >> 24) & 0xFF,
      (seconds >> 16) & 0xFF,
      (seconds >>  8) & 0xFF,
       seconds        & 0xFF,
    ]);
  }
}
```

- [ ] **Step 2: 替换 watch_time_protocol_test.dart**

测试向量（由 Python `calendar.timegm` 计算，TZ 无关）：
- `DateTime(2024, 7, 27, 15, 42, 36)` → `1722094956` → `[0x66, 0xA5, 0x15, 0x6C]`
- `DateTime(2000, 1, 1)` → `946684800` → `[0x38, 0x6D, 0x43, 0x80]`
- `DateTime(2063, 12, 31, 23, 59, 59)` → `2966371199` → `[0xB0, 0xCF, 0x3B, 0x7F]`

```dart
import 'package:flutter_test/flutter_test.dart';
import 'package:honeybox/services/watch_time_protocol.dart';

void main() {
  group('WatchTimeProtocol', () {
    test('builds an exact big-endian wall clock seconds frame', () {
      final frame = WatchTimeProtocol.buildSetTime(
        DateTime(2024, 7, 27, 15, 42, 36),
      );

      expect(
        frame,
        [0x02, 0x00, 0x01, 0x00, 0x04, 0x66, 0xA5, 0x15, 0x6C],
      );
    });

    test('year 2000 encodes correctly', () {
      final frame = WatchTimeProtocol.buildSetTime(DateTime(2000, 1, 1));
      expect(frame.sublist(5), [0x38, 0x6D, 0x43, 0x80]);
    });

    test('year 2063 encodes correctly', () {
      final frame = WatchTimeProtocol.buildSetTime(
        DateTime(2063, 12, 31, 23, 59, 59),
      );
      expect(frame.sublist(5), [0xB0, 0xCF, 0x3B, 0x7F]);
    });

    test('rejects years outside the protocol range', () {
      expect(
        () => WatchTimeProtocol.buildSetTime(DateTime(1969, 12, 31)),
        throwsArgumentError,
      );
      expect(
        () => WatchTimeProtocol.buildSetTime(DateTime(2107, 1, 1)),
        throwsArgumentError,
      );
    });
  });
}
```

- [ ] **Step 3: 运行手机测试**

```bash
cd "/mnt/c/Users/howie_wang.RSDOMAIN/workspace/HoneyBox"
cmd.exe /c "D:\\Android-dev-tool\\flutter\\bin\\flutter.bat test test\\services\\watch_time_protocol_test.dart"
```

期望：`All tests passed!`

- [ ] **Step 4: Commit（在 HoneyBox 仓库）**

```bash
cd "/mnt/c/Users/howie_wang.RSDOMAIN/workspace/HoneyBox"
git add lib/services/watch_time_protocol.dart test/services/watch_time_protocol_test.dart
git commit -m "feat(time): send 4-byte big-endian wall clock seconds instead of packed calendar"
```

---

## Task 10: 修改手机 watch_health_protocol.dart 与测试

**Files:**
- Modify: `HoneyBox/lib/services/watch_health_protocol.dart:300-307`
- Modify: `HoneyBox/test/services/watch_health_protocol_test.dart:49-52`

- [ ] **Step 1: 修改 _readTimestamp — 去掉 .toLocal()**

将第 300-307 行：
```dart
  /// Unix epoch seconds, big-endian. Records are UTC on the wire.
  static DateTime _readTimestamp(Uint8List bytes, int offset) {
    final seconds = (bytes[offset] << 24) |
        (bytes[offset + 1] << 16) |
        (bytes[offset + 2] << 8) |
        bytes[offset + 3];
    return DateTime.fromMillisecondsSinceEpoch(seconds * 1000, isUtc: true)
        .toLocal();
  }
```
改为：
```dart
  /// Wall clock seconds, big-endian (1970 epoch, value = what the watch shows).
  static DateTime _readTimestamp(Uint8List bytes, int offset) {
    final seconds = (bytes[offset] << 24) |
        (bytes[offset + 1] << 16) |
        (bytes[offset + 2] << 8) |
        bytes[offset + 3];
    return DateTime.fromMillisecondsSinceEpoch(seconds * 1000, isUtc: true);
  }
```

- [ ] **Step 2: 修改 watch_health_protocol_test.dart**

将第 49-52 行断言：
```dart
    expect(
      item.timestamp,
      DateTime.fromMillisecondsSinceEpoch(0x12345678 * 1000, isUtc: true)
          .toLocal(),
    );
```
改为：
```dart
    expect(
      item.timestamp,
      DateTime.fromMillisecondsSinceEpoch(0x12345678 * 1000, isUtc: true),
    );
```

- [ ] **Step 3: 运行手机测试**

```bash
cd "/mnt/c/Users/howie_wang.RSDOMAIN/workspace/HoneyBox"
cmd.exe /c "D:\\Android-dev-tool\\flutter\\bin\\flutter.bat test test\\services\\watch_health_protocol_test.dart"
```

期望：`All tests passed!`

- [ ] **Step 4: Commit**

```bash
git add lib/services/watch_health_protocol.dart test/services/watch_health_protocol_test.dart
git commit -m "fix(health): remove .toLocal() from timestamp decode — wall clock seconds need no tz shift"
```

---

## Task 11: 修改 watch_health_data.dart 与 fixture

**Files:**
- Modify: `HoneyBox/lib/pages/watch/health/watch_health_data.dart`
- Modify: `HoneyBox/test/helpers/watch_health_fixture.dart`

背景：`_readTimestamp` 现在返回 UTC-flagged `DateTime`（`isUtc: true`）。Dart 的 `.year`/`.month`/`.day`/`.hour` 对 UTC-flagged 对象返回 UTC 字段，对本地对象返回本地字段。`_today` 和 `syncedAt` 来自 `DateTime.now()`（本地），所以需要统一。

- [ ] **Step 1: 修改 _today getter（第 103 行）**

将：
```dart
  DateTime get _today => DateTime(syncedAt.year, syncedAt.month, syncedAt.day);
```
改为：
```dart
  DateTime get _today => DateTime.utc(syncedAt.year, syncedAt.month, syncedAt.day);
```

- [ ] **Step 2: 修改 _dailyTrend 中 firstDay（第 230 行）**

`firstDay` 和 `date` 通过 `_today.subtract(...)` / `firstDay.add(...)` 生成，`_today` 改为 UTC-flagged 后这两行自动正确，无需改动（Dart 的 `subtract`/`add` 保留 `isUtc` 标志）。

确认一遍：
```dart
    final firstDay = _today.subtract(Duration(days: days - 1));
    ...
    final date = firstDay.add(Duration(days: i));
```
`_today` 是 UTC-flagged → `firstDay`/`date` 也是 UTC-flagged → `.month`/`.day` 返回 UTC 字段 → 与 UTC-flagged 的 `record.timestamp` 一致。✓

- [ ] **Step 3: 确认 _sameDate 无需改动**

```dart
  static bool _sameDate(DateTime a, DateTime b) =>
      a.year == b.year && a.month == b.month && a.day == b.day;
```
两侧统一为 UTC-flagged 后，该函数正确工作，无需改动。

- [ ] **Step 4: 确认 _dayTrend 中 .hour 比较正确**

```dart
          record.timestamp.hour >= startHour &&
          record.timestamp.hour < endHour
```
`record.timestamp` 是 UTC-flagged → `.hour` 返回 UTC 小时 → 与墙上时钟小时一致（因为 wall clock seconds 本身就是"表盘时间"）。✓ 无需改动。

- [ ] **Step 5: 修改 watch_health_fixture.dart**

fixture 现在应使用 UTC-flagged `DateTime.utc(...)` 以匹配线上数据的实际类型：

将所有 `DateTime(2026, 7, ...)` / `DateTime(2026, 7, ...)` 改为 `DateTime.utc(2026, 7, ...)`：

```dart
/// Timestamps use DateTime.utc() to match the wire format:
/// _readTimestamp() returns UTC-flagged objects (isUtc: true).
WatchHealthSnapshot watchHealthFixture() => WatchHealthSnapshot.fromRecords(
      syncedAt: DateTime(2026, 7, 30, 12),   // syncedAt stays local (from DateTime.now)
      sportRecords: [
        WatchSportRecord(
          timestamp: DateTime.utc(2026, 7, 30, 8),
          mode: WatchSportMode.run,
          steps: 600,
          caloriesDeciKcal: 125,
          distanceMeters: 420,
          heartRate: 72,
          hasHeartRate: true,
        ),
        WatchSportRecord(
          timestamp: DateTime.utc(2026, 7, 30, 8, 15),
          mode: WatchSportMode.invalid,
          steps: 400,
          caloriesDeciKcal: 75,
          distanceMeters: 280,
          heartRate: 78,
          hasHeartRate: true,
        ),
      ],
      sleepRecords: [
        WatchSleepRecord(
          timestamp: DateTime.utc(2026, 7, 29, 23),
          state: WatchSleepState.deep,
        ),
        WatchSleepRecord(
          timestamp: DateTime.utc(2026, 7, 30, 1),
          state: WatchSleepState.light,
        ),
        WatchSleepRecord(
          timestamp: DateTime.utc(2026, 7, 30, 3),
          state: WatchSleepState.wake,
        ),
      ],
    );
```

- [ ] **Step 6: Commit**

```bash
cd "/mnt/c/Users/howie_wang.RSDOMAIN/workspace/HoneyBox"
git add lib/pages/watch/health/watch_health_data.dart test/helpers/watch_health_fixture.dart
git commit -m "fix(health-data): align _today to UTC-flagged DateTime to match wall clock timestamps"
```

---

## Task 12: 最终验收

- [ ] **Step 1: 固件宿主测试三遍通过**

```bash
cd tests/app_time && make run
```

期望：`TZ=`、`TZ=America/New_York`、`TZ=Asia/Shanghai` 下各输出 `all passed`。

- [ ] **Step 2: west build 通过**

```bash
west build -b rtl87x3g_evb
```

- [ ] **Step 3: 手机全量测试通过**

```bash
cd "/mnt/c/Users/howie_wang.RSDOMAIN/workspace/HoneyBox"
cmd.exe /c "D:\\Android-dev-tool\\flutter\\bin\\flutter.bat test"
```

- [ ] **Step 4: 硬件端到端验证（手工）**

1. 刷新固件（`west flash`）
2. App 绑定设备，确认时间同步成功
3. 观察 SPORT 同步日志时间与表盘一致（不偏 8 小时）
4. 次日 00:00 确认步数归零，而非 16:00

- [ ] **Step 5: Commit 验收记录**

```bash
cd /home/howie_wang/hmi/rtl8773g-simple-test/applications
git add docs/superpowers/plans/2026-08-25-unified-wall-clock-seconds.md
git commit -m "docs: add implementation plan for unified wall clock seconds"
```
