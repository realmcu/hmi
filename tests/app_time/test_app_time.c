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

static void test_now_is_unshifted(void)
{
    stub_set_rtc(2026, 8, 25, 15, 30, 0);
    uint32_t s = app_time_now();
    CHECK(s == WCS_1530, "app_time_now=%u want %u", (unsigned)s, WCS_1530);
}

static void test_set_roundtrips_and_fills_wday(void)
{
    memset(&stub_rtc, 0, sizeof(stub_rtc));
    int rc = app_time_set(WCS_1530);
    CHECK(rc == 0, "app_time_set rc=%d", rc);
    CHECK(stub_rtc.year == 2026 && stub_rtc.month == 8 && stub_rtc.mday == 25,
          "date %u-%u-%u", (unsigned)stub_rtc.year, (unsigned)stub_rtc.month, (unsigned)stub_rtc.mday);
    CHECK(stub_rtc.hour == 15 && stub_rtc.minute == 30 && stub_rtc.second == 0,
          "time %u:%u:%u", (unsigned)stub_rtc.hour, (unsigned)stub_rtc.minute, (unsigned)stub_rtc.second);
    /* 2026-08-25 is Tuesday -> wday 2 */
    CHECK(stub_rtc.wday == 2, "wday=%u want 2 (Tuesday)", (unsigned)stub_rtc.wday);
    CHECK(app_time_now() == WCS_1530, "set/now not identity");
}

static void test_to_calendar(void)
{
    struct tm t; memset(&t, 0, sizeof(t));
    app_time_to_calendar(WCS_1530, &t);
    CHECK(t.tm_year + 1900 == 2026, "year=%d", t.tm_year + 1900);
    CHECK(t.tm_mon + 1 == 8, "mon=%d", t.tm_mon + 1);
    CHECK(t.tm_mday == 25, "mday=%d", t.tm_mday);
    CHECK(t.tm_hour == 15, "hour=%d", t.tm_hour);
    CHECK(t.tm_min == 30, "min=%d", t.tm_min);
    CHECK(t.tm_wday == 2, "wday=%d", t.tm_wday);
}

static void test_day_changed_at_midnight(void)
{
    stub_set_rtc(2026, 8, 24, 23, 59, 59);
    stub_events_reset(); stub_tick();
    CHECK(stub_event_count_of(EVT_TIME_DAY_CHANGED) == 0, "day event on first tick");

    stub_set_rtc(2026, 8, 25, 0, 0, 0);
    stub_events_reset(); stub_tick();
    CHECK(stub_event_count_of(EVT_TIME_DAY_CHANGED) == 1, "no DAY_CHANGED at midnight");
    CHECK(stub_event_sec_of(EVT_TIME_DAY_CHANGED) == WCS_0000,
          "DAY_CHANGED sec=%u want %u", (unsigned)stub_event_sec_of(EVT_TIME_DAY_CHANGED), WCS_0000);
}

static void test_no_day_change_at_1600(void)
{
    stub_set_rtc(2026, 8, 25, 15, 59, 59);
    stub_events_reset(); stub_tick();   /* adopt day */
    stub_set_rtc(2026, 8, 25, 16, 0, 0);
    stub_events_reset(); stub_tick();
    CHECK(stub_event_count_of(EVT_TIME_DAY_CHANGED) == 0, "DAY_CHANGED at 16:00 => tz leak");
    CHECK(stub_event_count_of(EVT_TIME_TICK_15MIN) == 1, "16:00 not a quarter-hour boundary");
}

static void test_quarter_hour_boundaries(void)
{
    stub_set_rtc(2026, 8, 25, 10, 15, 0);
    stub_events_reset(); stub_tick();
    CHECK(stub_event_count_of(EVT_TIME_TICK_15MIN) == 1, "10:15 not a boundary");
    stub_set_rtc(2026, 8, 25, 10, 16, 0);
    stub_events_reset(); stub_tick();
    CHECK(stub_event_count_of(EVT_TIME_TICK_15MIN) == 0, "10:16 fired a tick");
}

static void test_unset_clock_is_silent(void)
{
    stub_set_rtc(1969, 1, 1, 0, 0, 0);
    stub_events_reset(); stub_tick();
    CHECK(stub_event_count == 0, "events with clock unset");
}

static void test_null_to_calendar_is_noop(void)
{
    /* must not crash */
    app_time_to_calendar(WCS_1530, NULL);
    CHECK(1, "null handling ok");   /* reaching here is the pass */
}

int main(void)
{
    const char *tz = getenv("TZ");
    printf("app_time tests (TZ=%s)\n", (tz && *tz) ? tz : "<unset>");
    stub_set_rtc(2026, 8, 25, 15, 30, 0);
    int rc = app_time_module.init();
    CHECK(rc == 0, "init rc=%d", rc);
    CHECK(stub_tick_cb != NULL, "1Hz cb not bound");

    test_now_is_unshifted();
    test_set_roundtrips_and_fills_wday();
    test_to_calendar();
    test_day_changed_at_midnight();
    test_no_day_change_at_1600();
    test_quarter_hour_boundaries();
    test_unset_clock_is_silent();
    test_null_to_calendar_is_noop();

    if (g_fail == 0) { printf("  all passed\n"); return 0; }
    printf("  %u check(s) failed\n", g_fail);
    return 1;
}
