/*
 * app_time.c — skeleton + demo event producer.
 *
 * TODO: read/write RTC via Zephyr rtc driver (or platform equivalent),
 *       run a per-minute local-time tick, publish EVT_TIME_SYNCED / _TICK_MIN.
 *
 * For now this file also acts as the demo publisher for the event bus:
 * the shell command `app_time tick [count]` publishes EVT_TIME_TICK_MIN
 * with a running counter, letting app_ble / app_health verify their
 * subscription paths.
 */

#include "app_time.h"
#include "app_event.h"
#include "app_event_defs.h"
#include "app_log.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

APP_LOG_MODULE_REGISTER(app_time);

static int  time_init(void) { return 0; }
const app_module_t app_time_module =
{
    .name  = "time",
    .init  = time_init,
};

/* -------- Synchronous API (empty skeleton bodies) -------- */

uint32_t app_time_now(void)                                       { return 0; }
int16_t  app_time_tz_offset(void)                                 { return 0; }
int      app_time_tz_set(int16_t minutes)                         { (void)minutes; return -1; }
int      app_time_set_from_phone(uint32_t unix_sec, int16_t tz)   { (void)unix_sec; (void)tz; return -1; }

void app_time_local_now(app_time_local_t *out)
{
    if (out != NULL)
    {
        memset(out, 0, sizeof(*out));
    }
}

/* -------- Shell-driven demo publisher --------
 *
 * `app_time tick`         → publish one EVT_TIME_TICK_MIN
 * `app_time tick <N>`     → publish N ticks in a row
 *
 * Each publish carries a u32 counter as the payload so subscribers can
 * verify ordering.
 */

#if defined(__ZEPHYR__) && defined(CONFIG_SHELL)

#include <zephyr/shell/shell.h>
#include <stdlib.h>

static uint32_t s_tick_counter;

static int cmd_app_time_tick(const struct shell *sh, size_t argc, char **argv)
{
    unsigned long n = 1;
    if (argc >= 2)
    {
        n = strtoul(argv[1], NULL, 0);
        if (n == 0)
        {
            n = 1;
        }
    }

    for (unsigned long i = 0; i < n; ++i)
    {
        s_tick_counter++;
        int rc = app_event_publish(EVT_TIME_TICK_MIN,
                                   &s_tick_counter,
                                   sizeof(s_tick_counter));
        if (rc != 0)
        {
            shell_error(sh, "publish failed rc=%d at tick %u", rc, s_tick_counter);
            return -1;
        }
    }
    shell_print(sh, "published %lu tick(s), latest counter=%u", n, s_tick_counter);
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(app_time_cmds,
                               SHELL_CMD_ARG(tick, NULL,
                                             "Publish EVT_TIME_TICK_MIN [count times] to exercise the event bus",
                                             cmd_app_time_tick, 1, 1),
                               SHELL_SUBCMD_SET_END
                              );

SHELL_CMD_REGISTER(app_time, &app_time_cmds,
                   "app_time demo commands (event-bus test)", NULL);

#endif /* CONFIG_SHELL */
