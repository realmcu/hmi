# Unified wall clock seconds implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Unify the time scalar across protocol, firmware and phone App onto a single wall clock seconds value (1970 epoch, anchored by the phone, never shifted by the firmware), and eradicate the existing double-timezone-offset defect.

**Architecture:** The phone converts the user's local calendar to `uint32_t` seconds by UTC rules and sends it; the firmware writes it into the RTC unchanged, reads it back unchanged and publishes events unchanged. The protocol 0x01 key changes from 32-bit packed calendar fields to 4 bytes of big-endian seconds. `app_time_local_t` and all timezone-offset code are deleted, and `struct tm` + `gmtime_r` take over calendar expansion.

**Tech Stack:** C11 (firmware, newlib + Zephyr), Dart/Flutter (phone), host gcc host tests (no Zephyr, stubbed posix + event bus), `cmd.exe /c "D:\\Android-dev-tool\\flutter\\bin\\flutter.bat test ..."` to run the Dart tests.

**Spec:** `docs/superpowers/specs/2026-08-25-unified-wall-clock-seconds-design.md`

## Global Constraints

- Breaking change: firmware and App **must** upgrade together; no version gate is introduced.
- The firmware must not use `mktime` / `localtime_r` (they depend on `TZ`); only `gmtime_r` + the hand-written `civil_to_epoch` are allowed.
- Every `DateTime.utc(...)` call on the phone must pass the six fields year/month/day/hour/minute/second explicitly; converting a locally constructed `DateTime(...)` is **not** allowed.
- The epoch is uniformly 1970-01-01 00:00:00; the `946684800` (2000 epoch) constant must not appear.
- Records already in Flash (the `ts_utc` field) hold the same numeric value under the new semantics, so **no migration** is needed, only a rename.
- The host tests must pass under all three of `TZ=` (unset), `TZ=America/New_York` and `TZ=Asia/Shanghai`.

---

## File Structure

| File | Action | Description |
|---|---|---|
| `app/app_time/app_time.h` | modify | Delete `app_time_local_t`, `app_time_set_local`, `app_time_to_local`; add `app_time_set(uint32_t)`, `app_time_to_calendar(uint32_t, struct tm*)` |
| `app/app_time/app_time.c` | modify | Delete `s_tz_min`, `local_sec_from_utc`, `app_time_to_local`, `app_time_set_local`; add `app_time_set`, `app_time_to_calendar`; change `rtc_second_cb` to drop the timezone shift |
| `app/app_core/app_event_defs.h` | modify | `app_evt_time_synced_t` replaced by a single `uint32_t sec` |
| `app/app_protocol/hmi_l2_cmd_settings.h` | modify | Delete the `hmi_l2_decode_time` declaration and the `app_time.h` include |
| `app/app_protocol/hmi_l2_cmd_settings.c` | modify | Delete `is_leap_year`, `days_in_month`, `hmi_l2_decode_time`; read 4 bytes of big-endian seconds instead |
| `app/app_health/app_health_internal.h` | modify | `ts_utc` → `ts` (field name); comment updated |
| `app/app_health/health_worker.c` | modify | `ts_utc` → `ts`; `boundary_utc` → `boundary_sec` |
| `app/app_health/health_db.c` | modify | `ts_utc` → `ts` (10 places); `health_db_save_synced_ts` parameter name |
| `app/app_protocol/hmi_l2_cmd_sport.c` | modify | `r->ts_utc` → `r->ts`; `app_time_to_local` → `app_time_to_calendar`; log formatting changed to `tm_year+1900`/`tm_mon+1` |
| `component/protocol/hmi_l2.h` | modify | Delete `HMI_L2_SET_ALARM`, `HMI_L2_GET_ALARM_REQ`, `HMI_L2_GET_ALARM_RSP` |
| `component/protocol/BLE_PROTOCOL_SPEC.html` | modify | 0x01 table switched to seconds; 0x02/0x03/0x04 alarm sections deleted; 0x21 epoch changed to 1970; uplink Timestamp wording updated |
| `tests/app_time/posix_stub.h` | create | posix + event bus stub header for the host tests |
| `tests/app_time/posix_stub.c` | create | stub implementation |
| `tests/app_time/test_app_time.c` | create | 7 host unit tests |
| `tests/app_time/Makefile` | create | `make run` runs the tests under three TZs |
| `HoneyBox/lib/services/watch_time_protocol.dart` | modify | packed bit operations → 4 bytes of big-endian seconds; `DateTime.utc(...)` conversion |
| `HoneyBox/test/services/watch_time_protocol_test.dart` | modify | update the assertion vectors |
| `HoneyBox/lib/services/watch_health_protocol.dart` | modify | `_readTimestamp` drops `.toLocal()` |
| `HoneyBox/test/services/watch_health_protocol_test.dart` | modify | drop `.toLocal()` |
| `HoneyBox/lib/pages/watch/health/watch_health_data.dart` | modify | `_today` and 6 comparison sites changed to UTC-flagged |
| `HoneyBox/test/helpers/watch_health_fixture.dart` | modify | timestamps changed to UTC-flagged `DateTime.utc(...)` |

---

## Task 1: Set up the host test skeleton (write the failing tests first)

**Files:**
- Create: `tests/app_time/posix_stub.h`
- Create: `tests/app_time/posix_stub.c`
- Create: `tests/app_time/test_app_time.c`
- Create: `tests/app_time/Makefile`

**Interfaces:**
- Consumes: `app/app_time/app_time.h` (current version), `app/app_core/app_event_defs.h`
- Produces: `stub_set_rtc()`, `stub_tick()`, `stub_events_reset()`, `stub_event_count_of()`, `stub_event_sec_of()` for the Task 2 tests

- [ ] **Step 1: Write posix_stub.h**

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

- [ ] **Step 2: Write posix_stub.c**

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

- [ ] **Step 3: Write test_app_time.c (7 tests, all using the new API)**

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

- [ ] **Step 4: Write the Makefile**

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

- [ ] **Step 5: Run it and confirm the build fails (the new API does not exist yet)**

```bash
cd tests/app_time && make run
```

Expected: build errors `implicit declaration of function 'app_time_set'` and `app_time_to_calendar`.

- [ ] **Step 6: Commit the test skeleton**

```bash
git add tests/app_time/
git commit -m "test(app_time): add host unit test harness (failing – new API not yet impl)"
```

---

## Task 2: Modify app_time.h / app_time.c

**Files:**
- Modify: `app/app_time/app_time.h`
- Modify: `app/app_time/app_time.c`

**Interfaces:**
- Produces: `app_time_set(uint32_t sec)` → `int`; `app_time_to_calendar(uint32_t sec, struct tm *out)` → `void`
- Removes: `app_time_local_t`, `app_time_set_local`, `app_time_to_local`, `s_tz_min`, `local_sec_from_utc`
- Keeps: `app_time_now()`, `civil_to_epoch()` (static, used by `app_time_now` to read the RTC)

- [ ] **Step 1: Replace app_time.h**

Full new content:

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

- [ ] **Step 2: Replace app_time.c**

Full new content (the key change points are commented below; the complete file is in Spec §5.1):

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

- [ ] **Step 3: Run the tests and confirm everything passes**

```bash
cd tests/app_time && make run
```

Expected (three runs): `all passed`

- [ ] **Step 4: Commit**

```bash
git add app/app_time/app_time.h app/app_time/app_time.c tests/app_time/
git commit -m "feat(app_time): unified wall clock seconds — delete tz offset, add app_time_set/to_calendar"
```

---

## Task 3: Modify app_event_defs.h — app_evt_time_synced_t

**Files:**
- Modify: `app/app_core/app_event_defs.h:125-144`

**Interfaces:**
- Removes: the 6 calendar fields of `app_evt_time_synced_t`
- Produces: `typedef struct { uint32_t sec; } app_evt_time_synced_t;`

- [ ] **Step 1: Locate and replace the struct definition**

Replace lines 125-144 of `app_event_defs.h` (the `app_evt_time_synced_t` comment + struct) with:

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

Also remove the `UTC + timezone offset` wording from the Time section comment on lines 36-44 of `app_event_defs.h`, changing it to:

```c
    /* Time (0x300~)
     *
     * Tick events fire on wall-clock boundaries -- the scalar is
     * wall clock seconds (1970 epoch), which IS the moment the watch
     * face shows. No timezone offset exists anywhere in this path.
     */
```

- [ ] **Step 2: Confirm the on_evt_time_synced callers do not break**

```bash
grep -rn "app_evt_time_synced_t\|\.year\b\|\.month\b\|\.day\b\|\.hour\b\|\.min\b\|\.sec\b" \
     app/app_health/app_health.c app/app_time/app_time.c app/app_protocol/hmi_l2_cmd_settings.c
```

The `on_evt_time_synced` in `app_health.c` already has `(void)payload`, accesses no fields and needs no change.
The `on_evt_time_synced` in `app_time.c` likewise ignores the payload.
Only `hmi_l2_cmd_settings.c` fills that struct; it is handled in Task 4.

- [ ] **Step 3: Commit**

```bash
git add app/app_core/app_event_defs.h
git commit -m "refactor(event): simplify app_evt_time_synced_t to single uint32_t sec"
```

---

## Task 4: Modify hmi_l2_cmd_settings.h / .c

**Files:**
- Modify: `app/app_protocol/hmi_l2_cmd_settings.h`
- Modify: `app/app_protocol/hmi_l2_cmd_settings.c`

**Interfaces:**
- Removes: `hmi_l2_decode_time`, `is_leap_year`, `days_in_month`
- Consumes: `app_time_set(uint32_t)` (Task 2), `app_evt_time_synced_t.sec` (Task 3)

- [ ] **Step 1: Replace hmi_l2_cmd_settings.h**

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

(delete the `app_time.h` include and the `hmi_l2_decode_time` declaration)

- [ ] **Step 2: Replace hmi_l2_cmd_settings.c**

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

- [ ] **Step 3: west build to confirm it compiles**

```bash
west build -b rtl87x3g_evb
```

- [ ] **Step 4: Commit**

```bash
git add app/app_protocol/hmi_l2_cmd_settings.h app/app_protocol/hmi_l2_cmd_settings.c
git commit -m "feat(settings): decode 0x01 as 4-byte big-endian wall clock seconds"
```

---

## Task 5: Modify hmi_l2.h — delete the alarm macros

**Files:**
- Modify: `component/protocol/hmi_l2.h:29-32`

**Interfaces:**
- Removes: `HMI_L2_SET_ALARM 0x02u`, `HMI_L2_GET_ALARM_REQ 0x03u`, `HMI_L2_GET_ALARM_RSP 0x04u` (no references, safe to delete)

- [ ] **Step 1: Confirm there are no references**

```bash
grep -rn "HMI_L2_SET_ALARM\|HMI_L2_GET_ALARM" --include=*.c --include=*.h . | grep -v build
```

Expected: zero results.

- [ ] **Step 2: Delete the three macro definitions**

In `hmi_l2.h`:
```c
#define HMI_L2_SET_ALARM        0x02u
#define HMI_L2_GET_ALARM_REQ    0x03u
#define HMI_L2_GET_ALARM_RSP    0x04u
```
Delete those three lines (keep the `HMI_L2_SET_TIME 0x01u` line, so `HMI_L2_SET_STEP_TARGET 0x05u` follows directly).

- [ ] **Step 3: west build to confirm there are no build errors**

```bash
west build -b rtl87x3g_evb
```

- [ ] **Step 4: Commit**

```bash
git add component/protocol/hmi_l2.h
git commit -m "refactor(protocol): remove unimplemented alarm key macros (0x02/0x03/0x04)"
```

---

## Task 6: Modify the health layer — rename ts_utc → ts

**Files:**
- Modify: `app/app_health/app_health_internal.h:63`
- Modify: `app/app_health/health_worker.c` (`ts_utc`, `boundary_utc`)
- Modify: `app/app_health/health_db.c` (`ts_utc` in 10 places, the `health_db_save_synced_ts` parameter name)

**Interfaces:**
- `health_pedo_record_t.ts_utc` → `.ts` (type, offset and layout unchanged; Flash records are unaffected)
- `health_worker_on_bucket_boundary(uint32_t boundary_utc)` → `(uint32_t boundary_sec)`

- [ ] **Step 1: Modify app_health_internal.h**

Change line 63:
```c
    uint32_t ts_utc;        /* UTC epoch seconds captured at flush moment  */
```
to:
```c
    uint32_t ts;            /* wall clock seconds at flush moment           */
```

Change the comment on line 108 from `using the record's ts_utc as` to `using the record's ts as`.

Change the comment and function signature on lines 150-156:
```c
/* Bucket boundary reached: write the closed bucket to the TSDB. @c boundary_utc
 * ...
void health_worker_on_bucket_boundary(uint32_t boundary_utc);
```
to:
```c
/* Bucket boundary reached: write the closed bucket to the TSDB. @c boundary_sec
 * is the boundary instant in wall clock seconds and becomes the record's
 * timestamp. A no-op when no worker is running. */
void health_worker_on_bucket_boundary(uint32_t boundary_sec);
```

- [ ] **Step 2: Modify health_worker.c**

Replace `ts_utc` → `ts` throughout (affects the initializer `.ts_utc = ts` on line 280).  
Replace `boundary_utc` → `boundary_sec` throughout (affects lines 222, 236, 269, 328, 330 and 337).  
Change the comment on line 222 from `@c boundary_utc` to `@c boundary_sec`.

- [ ] **Step 3: Modify health_db.c**

Replace `ts_utc` → `ts` throughout, 10 places in total:
- the comments on lines 8 and 13
- the code on lines 92, 96, 99, 101, 105, 159 (parameter name), 170, 224, 267, 291, 378 and 380

In `health_db_save_synced_ts` the parameter `uint32_t ts_utc` → `uint32_t ts`, and in the function body `&ts_utc` → `&ts`.

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

## Task 7: Modify hmi_l2_cmd_sport.c — adapt to the new API

**Files:**
- Modify: `app/app_protocol/hmi_l2_cmd_sport.c:102-105,185-193`

**Interfaces:**
- `r->ts_utc` → `r->ts` (after the Task 6 rename)
- `app_time_to_local(...)` → `app_time_to_calendar(...)` (Task 2)
- Log formatting: `lt.year` → `tm.tm_year+1900`, `lt.month` → `tm.tm_mon+1`

- [ ] **Step 1: Change the sport_encode_record field names**

On lines 102-105, change `r->ts_utc` to `r->ts` (4 occurrences):

```c
    out[p++] = (uint8_t)(r->ts >> 24);
    out[p++] = (uint8_t)(r->ts >> 16);
    out[p++] = (uint8_t)(r->ts >>  8);
    out[p++] = (uint8_t)(r->ts);
```

- [ ] **Step 2: Change the logging call**

Change lines 185-193:
```c
        app_time_local_t lt;
        app_time_to_local(rec.ts_utc, &lt);
        PROTO_LOG("L2 SPORT rec %u: %04u-%02u-%02u %02u:%02u steps=%u",
                  (unsigned)n, (unsigned)lt.year, (unsigned)lt.month,
                  (unsigned)lt.day, (unsigned)lt.hour, (unsigned)lt.min,
                  (unsigned)rec.steps);
```
to:
```c
        struct tm lt;
        app_time_to_calendar(rec.ts, &lt);
        PROTO_LOG("L2 SPORT rec %u: %04u-%02u-%02u %02u:%02u steps=%u",
                  (unsigned)n,
                  (unsigned)(lt.tm_year + 1900), (unsigned)(lt.tm_mon + 1),
                  (unsigned)lt.tm_mday, (unsigned)lt.tm_hour,
                  (unsigned)lt.tm_min, (unsigned)rec.steps);
```

Add `#include <time.h>` to the includes at the top of the file (if not already present).

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

## Task 8: Modify BLE_PROTOCOL_SPEC.html

**Files:**
- Modify: `component/protocol/BLE_PROTOCOL_SPEC.html`

Note: this file lives in the `component/protocol/` sub-repository and must be committed there separately.

- [ ] **Step 1: Replace the 0x01 time set table**

Locate line 711, the `<h4>` heading for the 0x01 time set section.  
Replace the whole `<table>` block that follows (6 rows: Year/Month/Day/Hour/Minute/Second) with:

```html
<h4 id="0x01-time-set">0x01 — Time set</h4>
<p><strong>Direction</strong>: phone → device (must be synced after every successful bind)<br />
<strong>Value (4 bytes, Big-Endian)</strong>: wall clock seconds, counted from 1970-01-01 00:00:00.</p>
<table>
<thead><tr><th>Byte order</th><th>Width</th><th>Description</th></tr></thead>
<tbody>
<tr><td>Big-endian</td><td>4 bytes</td><td>Its value is the instant the device should display. The phone converts the user's local calendar to seconds; <strong>the device performs no timezone handling</strong>. Valid range 1970-01-01 to 2106-02-07.</td></tr>
</tbody>
</table>
<p><strong>Breaking change</strong> (2026-08-25): the previous format was 32-bit packed calendar fields (Year 6 bits + Month 4 bits + Day 5 bits + Hour 5 bits + Minute 6 bits + Second 6 bits) and is now obsolete. Firmware and App must be upgraded in the same version.</p>
```

- [ ] **Step 2: Delete the three alarm TOC rows**

In the TOC table (around lines 660-680), delete:
```html
<tr>
<td><code>0x02</code></td>
<td>Alarm setting</td>
</tr>
<tr>
<td><code>0x03</code></td>
<td>Get device alarm list request</td>
</tr>
<tr>
<td><code>0x04</code></td>
<td>Get device alarm list response</td>
</tr>
```

- [ ] **Step 3: Delete the alarm section body**

Delete lines 754-812 (from the 0x02/0x04 alarm-settings heading through the end of the 0x03 get-device-alarm-list-request section, stopping at its closing `<p><strong>Value</strong>: empty</p>`; two subsections in total).

- [ ] **Step 4: Update the 0x21 Timestamp epoch**

Locate line 1634:
```html
<td>Seconds since 2000</td>
```
to:
```html
<td>Wall clock seconds (counted from 1970-01-01, consistent with the §0x01 definition)</td>
```

- [ ] **Step 5: Update the uplink Timestamp wording**

Locate lines 1178-1181 (the sport bucket description paragraph):
```html
<p>The phone must not treat Timestamp as the bucket start time, and must not apply any timezone,
Date/Offset or 15-minute shift conversion to Timestamp either.</p>
```
to:
```html
<p>Timestamp is the wall clock instant displayed by the device (wall clock seconds, 1970 epoch),
so the phone can render it directly without any timezone conversion.</p>
```

Locate line 1224:
```html
<td>Unix timestamp (seconds), consistent with the FlashDB record time</td>
```
to:
```html
<td>Wall clock seconds (counted from 1970-01-01, consistent with the device display), consistent with the FlashDB record time</td>
```

Locate line 1357 (sleep record):
```html
<td>Unix timestamp (seconds) at which the state actually took effect, consistent with the FlashDB record time</td>
```
to:
```html
<td>Wall clock seconds (counted from 1970-01-01) at which the state actually took effect, consistent with the FlashDB record time</td>
```

- [ ] **Step 6: Commit (in the component/protocol sub-repository)**

```bash
cd component/protocol
git add BLE_PROTOCOL_SPEC.html
git commit -m "docs: unify all timestamps to wall clock seconds (1970 epoch, breaking change)"
cd ../..
```

---

## Task 9: Modify the phone watch_time_protocol.dart

**Files:**
- Modify: `C:\Users\howie_wang.RSDOMAIN\workspace\HoneyBox\lib\services\watch_time_protocol.dart`
- Modify: `C:\Users\howie_wang.RSDOMAIN\workspace\HoneyBox\test\services\watch_time_protocol_test.dart`

WSL path: `/mnt/c/Users/howie_wang.RSDOMAIN/workspace/HoneyBox/...`

- [ ] **Step 1: Replace watch_time_protocol.dart**

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

- [ ] **Step 2: Replace watch_time_protocol_test.dart**

Test vectors (computed with Python `calendar.timegm`, timezone independent):
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

- [ ] **Step 3: Run the phone tests**

```bash
cd "/mnt/c/Users/howie_wang.RSDOMAIN/workspace/HoneyBox"
cmd.exe /c "D:\\Android-dev-tool\\flutter\\bin\\flutter.bat test test\\services\\watch_time_protocol_test.dart"
```

Expected: `All tests passed!`

- [ ] **Step 4: Commit (in the HoneyBox repo)**

```bash
cd "/mnt/c/Users/howie_wang.RSDOMAIN/workspace/HoneyBox"
git add lib/services/watch_time_protocol.dart test/services/watch_time_protocol_test.dart
git commit -m "feat(time): send 4-byte big-endian wall clock seconds instead of packed calendar"
```

---

## Task 10: Modify the phone watch_health_protocol.dart and its test

**Files:**
- Modify: `HoneyBox/lib/services/watch_health_protocol.dart:300-307`
- Modify: `HoneyBox/test/services/watch_health_protocol_test.dart:49-52`

- [ ] **Step 1: Change _readTimestamp — drop .toLocal()**

Change lines 300-307:
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
to:
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

- [ ] **Step 2: Modify watch_health_protocol_test.dart**

Change the assertion on lines 49-52:
```dart
    expect(
      item.timestamp,
      DateTime.fromMillisecondsSinceEpoch(0x12345678 * 1000, isUtc: true)
          .toLocal(),
    );
```
to:
```dart
    expect(
      item.timestamp,
      DateTime.fromMillisecondsSinceEpoch(0x12345678 * 1000, isUtc: true),
    );
```

- [ ] **Step 3: Run the phone tests**

```bash
cd "/mnt/c/Users/howie_wang.RSDOMAIN/workspace/HoneyBox"
cmd.exe /c "D:\\Android-dev-tool\\flutter\\bin\\flutter.bat test test\\services\\watch_health_protocol_test.dart"
```

Expected: `All tests passed!`

- [ ] **Step 4: Commit**

```bash
git add lib/services/watch_health_protocol.dart test/services/watch_health_protocol_test.dart
git commit -m "fix(health): remove .toLocal() from timestamp decode — wall clock seconds need no tz shift"
```

---

## Task 11: Modify watch_health_data.dart and the fixture

**Files:**
- Modify: `HoneyBox/lib/pages/watch/health/watch_health_data.dart`
- Modify: `HoneyBox/test/helpers/watch_health_fixture.dart`

Background: `_readTimestamp` now returns a UTC-flagged `DateTime` (`isUtc: true`). Dart's `.year`/`.month`/`.day`/`.hour` return UTC fields for UTC-flagged objects and local fields for local objects. `_today` and `syncedAt` come from `DateTime.now()` (local), so they must be brought into line.

- [ ] **Step 1: Change the _today getter (line 103)**

Change:
```dart
  DateTime get _today => DateTime(syncedAt.year, syncedAt.month, syncedAt.day);
```
to:
```dart
  DateTime get _today => DateTime.utc(syncedAt.year, syncedAt.month, syncedAt.day);
```

- [ ] **Step 2: Change firstDay in _dailyTrend (line 230)**

`firstDay` and `date` are produced through `_today.subtract(...)` / `firstDay.add(...)`; once `_today` is UTC-flagged those two lines are automatically correct and need no change (Dart's `subtract`/`add` preserve the `isUtc` flag).

Confirm once more:
```dart
    final firstDay = _today.subtract(Duration(days: days - 1));
    ...
    final date = firstDay.add(Duration(days: i));
```
`_today` is UTC-flagged → `firstDay`/`date` are UTC-flagged too → `.month`/`.day` return UTC fields → consistent with the UTC-flagged `record.timestamp`. ✓

- [ ] **Step 3: Confirm _sameDate needs no change**

```dart
  static bool _sameDate(DateTime a, DateTime b) =>
      a.year == b.year && a.month == b.month && a.day == b.day;
```
Once both sides are UTC-flagged the function works correctly and needs no change.

- [ ] **Step 4: Confirm the .hour comparison in _dayTrend is correct**

```dart
          record.timestamp.hour >= startHour &&
          record.timestamp.hour < endHour
```
`record.timestamp` is UTC-flagged → `.hour` returns the UTC hour → equal to the wall clock hour (because wall clock seconds are by definition "watch face time"). ✓ No change needed.

- [ ] **Step 5: Modify watch_health_fixture.dart**

The fixture should now use UTC-flagged `DateTime.utc(...)` to match the actual type of the wire data:

Change every `DateTime(2026, 7, ...)` to `DateTime.utc(2026, 7, ...)`:

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

## Task 12: Final acceptance

- [ ] **Step 1: Firmware host tests pass three times**

```bash
cd tests/app_time && make run
```

Expected: `all passed` under each of `TZ=`, `TZ=America/New_York` and `TZ=Asia/Shanghai`.

- [ ] **Step 2: west build passes**

```bash
west build -b rtl87x3g_evb
```

- [ ] **Step 3: The full phone test suite passes**

```bash
cd "/mnt/c/Users/howie_wang.RSDOMAIN/workspace/HoneyBox"
cmd.exe /c "D:\\Android-dev-tool\\flutter\\bin\\flutter.bat test"
```

- [ ] **Step 4: Hardware end-to-end verification (manual)**

1. Flash the firmware (`west flash`)
2. Bind the device from the App and confirm time sync succeeds
3. Check that the SPORT sync log time matches the watch face (no 8-hour shift)
4. At 00:00 the next day confirm the step count resets, rather than at 16:00

- [ ] **Step 5: Commit the acceptance record**

```bash
cd /home/howie_wang/hmi/rtl8773g-simple-test/applications
git add docs/superpowers/plans/2026-08-25-unified-wall-clock-seconds.md
git commit -m "docs: add implementation plan for unified wall clock seconds"
```
