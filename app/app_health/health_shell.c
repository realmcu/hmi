/*
 * health_shell.c
 *
 * `health` Zephyr shell command surface. Read-only access + a debug
 * flush. Acquisition and the 15-minute bucket boundary are owned by
 * the app_health module; this file only lets a developer inspect the
 * TSDB state from the console.
 *
 * Physically lives here (in app/app_health/) rather than under
 * component/gsensor-algorithm/ so the "app depends on component"
 * layering stays strict: components must not #include app-layer
 * headers, but the app layer is free to expose shell commands built
 * on its own APIs.
 *
 * Subcommands:
 *   health flush          debug: force flush accumulator NOW
 *                         (bypasses the 15-minute bucket boundary)
 *   health list [f] [t]   dump records in [f, t] UTC seconds, or all
 *   health count [f] [t]  count records in [f, t] UTC seconds, or all
 *   health clean          erase every record in the pedo TSDB
 *   health today          show current UTC-day rollup
 */

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#include "app_health.h"
#include "app_health_internal.h"

/* ----------------------------------------------------------------
 * Formatting helpers.
 * ---------------------------------------------------------------- */

static void fmt_utc(uint32_t ts, char *buf, size_t buf_len)
{
    if (ts == 0) { snprintf(buf, buf_len, "-"); return; }
    time_t t = (time_t)ts;
    struct tm z;
    gmtime_r(&t, &z);
    /* Clamp fields so GCC can prove the snprintf output fits the
     * caller's 24-byte buffer (-Wformat-truncation). gmtime_r already
     * bounds them at runtime; these modulos are for the compiler. */
    unsigned int y  = ((unsigned int)(z.tm_year + 1900)) % 10000;
    unsigned int mo = ((unsigned int)(z.tm_mon  + 1))    % 100;
    unsigned int d  = ((unsigned int)z.tm_mday)          % 100;
    unsigned int hh = ((unsigned int)z.tm_hour)          % 100;
    unsigned int mm = ((unsigned int)z.tm_min)           % 100;
    unsigned int ss = ((unsigned int)z.tm_sec)           % 100;
    snprintf(buf, buf_len, "%04u-%02u-%02u %02u:%02u:%02u",
             y, mo, d, hh, mm, ss);
}

static void dump_record(const struct shell *sh, int idx,
                        const health_pedo_record_t *r,
                        uint32_t fdb_ts, uint32_t addr)
{
    char t_rec[24], t_fdb[24];
    fmt_utc(r->ts_utc, t_rec, sizeof(t_rec));
    fmt_utc(fdb_ts,    t_fdb, sizeof(t_fdb));
    shell_print(sh, "[%d] fdb_ts=%s rec_ts=%s addr=0x%08x",
                idx, t_fdb, t_rec, (unsigned)addr);
    shell_print(sh,
                "     steps=%u dist=%um cal=%u.%u kcal hr=%u bucket=%umin mode=%u flags=0x%02x%s",
                r->steps, r->distance_m,
                r->calories_dkcal / 10, r->calories_dkcal % 10,
                r->hr_avg, r->bucket_min, r->mode, r->flags,
                (r->flags & HEALTH_RECORD_FLAG_PARTIAL_BUCKET) ? " partial" : "");
}

/* ----------------------------------------------------------------
 * Iteration callback.
 * ---------------------------------------------------------------- */

typedef struct
{
    const struct shell *sh;
    int                 n;
} list_ctx_t;

static bool list_cb(const health_pedo_record_t *rec,
                    uint32_t fdb_ts, uint32_t fdb_addr, void *user)
{
    list_ctx_t *ctx = (list_ctx_t *)user;
    dump_record(ctx->sh, ctx->n, rec, fdb_ts, fdb_addr);
    ctx->n++;
    return false;
}

/* ----------------------------------------------------------------
 * Command handlers.
 * ---------------------------------------------------------------- */

static bool parse_timestamp(const struct shell *sh, const char *text,
                            const char *name, uint32_t *out)
{
    if (text == NULL || out == NULL || text[0] == '\0' || text[0] == '-')
    {
        shell_error(sh, "%s must be an epoch timestamp in [0, %u]",
                    name, (unsigned)INT32_MAX);
        return false;
    }

    char *end = NULL;
    errno = 0;
    unsigned long value = strtoul(text, &end, 0);
    if (errno == ERANGE || end == text || *end != '\0' || value > INT32_MAX)
    {
        shell_error(sh, "invalid %s '%s'; expected [0, %u]",
                    name, text, (unsigned)INT32_MAX);
        return false;
    }

    *out = (uint32_t)value;
    return true;
}

static bool parse_range(const struct shell *sh, size_t argc, char **argv,
                        uint32_t *from, uint32_t *to)
{
    *from = 0u;
    *to = 0u;
    if (argc >= 2 && !parse_timestamp(sh, argv[1], "from_ts", from))
    {
        return false;
    }
    if (argc >= 3 && !parse_timestamp(sh, argv[2], "to_ts", to))
    {
        return false;
    }
    if (*to != 0u && *from > *to)
    {
        shell_error(sh, "from_ts must not be greater than to_ts");
        return false;
    }
    return true;
}

static int cmd_flush(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);
    shell_warn(sh, "debug flush: bypassing 15-min bucket boundary");
    switch (health_worker_flush_now())
    {
    case HEALTH_FLUSH_OK:
        shell_print(sh, "flushed one partial-bucket record");
        return 0;
    case HEALTH_FLUSH_EMPTY:
        shell_print(sh, "accumulator empty, nothing to flush");
        return 0;
    case HEALTH_FLUSH_NOT_RUNNING:
        shell_error(sh, "health worker is not running");
        return -EAGAIN;
    case HEALTH_FLUSH_INVALID_TIME:
        shell_error(sh, "RTC time is invalid; record was not flushed");
        return -ENODATA;
    case HEALTH_FLUSH_DB_ERROR:
    default:
        shell_error(sh, "TSDB append failed; accumulated data was retained");
        return -EIO;
    }
}

static int cmd_list(const struct shell *sh, size_t argc, char **argv)
{
    uint32_t from, to;
    if (!parse_range(sh, argc, argv, &from, &to))
    {
        return -EINVAL;
    }

    list_ctx_t ctx = { .sh = sh, .n = 0 };
    (void)health_db_iter(from, to, list_cb, &ctx);
    shell_print(sh, "list done, %d record(s)", ctx.n);
    return 0;
}

static int cmd_count(const struct shell *sh, size_t argc, char **argv)
{
    uint32_t from, to;
    if (!parse_range(sh, argc, argv, &from, &to))
    {
        return -EINVAL;
    }
    size_t n = health_db_count(from, to);
    shell_print(sh, "count [%u..%u] : %zu",
                (unsigned)from, (unsigned)to, n);
    return 0;
}

static int cmd_clean(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);
    if (health_db_clean() != 0)
    {
        shell_error(sh, "tsdb clean failed");
        return -EIO;
    }
    shell_print(sh, "cleaned");
    return 0;
}

static int cmd_today(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);
    health_daily_rollup_t t;
    app_health_get_today(&t);
    shell_print(sh,
                "today[day=%u]: steps=%u dist=%um cal=%u.%u kcal",
                (unsigned)t.utc_day_index,
                (unsigned)t.steps, (unsigned)t.distance_m,
                (unsigned)(t.calories_dkcal / 10u),
                (unsigned)(t.calories_dkcal % 10u));
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
    sub_health,
    SHELL_CMD_ARG(flush, NULL,
                  "debug: force-flush accumulator to fdb, bypassing 15min boundary",
                  cmd_flush, 1, 0),
    SHELL_CMD_ARG(list,  NULL,
                  "health list [from_ts] [to_ts] - dump TSDB records",
                  cmd_list, 1, 2),
    SHELL_CMD_ARG(count, NULL,
                  "health count [from_ts] [to_ts] - count TSDB records",
                  cmd_count, 1, 2),
    SHELL_CMD_ARG(clean, NULL,
                  "erase all pedo records in the pedo TSDB",
                  cmd_clean, 1, 0),
    SHELL_CMD_ARG(today, NULL,
                  "print current-day rollup (steps/distance/calories)",
                  cmd_today, 1, 0),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(health, &sub_health,
                   "pedo/activity data shell (flush|list|count|clean|today)",
                   NULL);
