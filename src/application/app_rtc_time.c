/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#include <time.h>
#include <string.h>
#include <trace.h>
#include "rtc_calendar.h"
#include "app_rtc_time.h"

#define RTC_CALENDAR_UPDATE_INTERVAL_S      10
/* seconds between 2000-01-01 00:00:00 (rtc_calendar epoch) and 1970-01-01 00:00:00 (time_t epoch) */
#define RTC_CALENDAR_TO_POSIX_EPOCH_OFFSET  946684800UL

static uint8_t app_rtc_time_month_from_str(const char *mon_str)
{
    static const char *const months[12] =
    {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };

    for (uint8_t i = 0; i < 12; i++)
    {
        if (memcmp(mon_str, months[i], 3) == 0)
        {
            return i + 1;
        }
    }
    return 1;
}

/* fall back default clock: parsed from compiler __DATE__/__TIME__, kept until synced by app */
static void app_rtc_time_get_build_time(T_UTC_TIME *utc_time)
{
    const char *build_date = __DATE__; /* "Mmm dd yyyy" */
    const char *build_time = __TIME__; /* "hh:mm:ss" */

    utc_time->month = app_rtc_time_month_from_str(build_date);
    utc_time->day = (build_date[4] == ' ' ? 0 : (build_date[4] - '0') * 10) +
                    (build_date[5] - '0');
    utc_time->year = (build_date[7] - '0') * 1000 + (build_date[8] - '0') * 100 +
                     (build_date[9] - '0') * 10 + (build_date[10] - '0');
    utc_time->hour = (build_time[0] - '0') * 10 + (build_time[1] - '0');
    utc_time->minutes = (build_time[3] - '0') * 10 + (build_time[4] - '0');
    utc_time->seconds = (build_time[6] - '0') * 10 + (build_time[7] - '0');
}

void app_rtc_time_init(void)
{
    T_UTC_TIME build_time;
    app_rtc_time_get_build_time(&build_time);

    if (!rtc_calendar_int(&build_time, RTC_CALENDAR_UPDATE_INTERVAL_S))
    {
        APP_PRINT_ERROR0("app_rtc_time_init: rtc_calendar_int failed");
    }
}

time_t time(time_t *t)
{
    time_t now = (time_t)(rtc_calendar_get_timestamp() + RTC_CALENDAR_TO_POSIX_EPOCH_OFFSET);

    if (t != NULL)
    {
        *t = now;
    }
    return now;
}
