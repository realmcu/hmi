#include "posix_port_fdb_priv.h"
#include "posix_init.h"
#include <string.h>

#ifdef FDB_USING_TSDB

/* Returning true stops iteration; we stop only on overflow. */
static bool ts_collect_cb(fdb_tsl_t tsl, void *arg)
{
    fdb_file_t *f = (fdb_file_t *)arg;
    if (f->ts_cache_count >= CONFIG_POSIX_FDB_TS_ITER_CACHE_MAX)
    {
        f->ts_iter_overflow = true;
        return true;
    }
    f->ts_addr_cache[f->ts_cache_count++] = tsl->addr.index;
    return false;
}

int ts_ioctl(fdb_inst_t *inst, fdb_file_t *f, unsigned long cmd, void *arg)
{
    switch (cmd)
    {
    case POSIX_FDB_TS_IOCTL_APPEND:
    {
        if (arg == NULL)
        {
            return POSIX_ERR_INVAL;
        }
        posix_fdb_ts_append_t *a = (posix_fdb_ts_append_t *)arg;
        if (a->buf == NULL || a->len == 0)
        {
            return POSIX_ERR_INVAL;
        }
        struct fdb_blob blob;
        fdb_blob_make(&blob, (void *)a->buf, a->len);
        fdb_err_t e;
        if (a->ts == POSIX_FDB_TS_AUTO_TIME)
        {
            e = fdb_tsl_append(inst->u.ts, &blob);
        }
        else
        {
            e = fdb_tsl_append_with_ts(inst->u.ts, &blob, (fdb_time_t)a->ts);
        }
        return fdb_err_to_posix(e);
    }
    case POSIX_FDB_TS_IOCTL_ITER_INIT:
    {
        if (arg == NULL)
        {
            return POSIX_ERR_INVAL;
        }
        posix_fdb_ts_iter_init_t *cfg = (posix_fdb_ts_iter_init_t *)arg;
        f->ts_cache_count = 0;
        f->ts_cache_pos = 0;
        f->ts_iter_overflow = false;
        f->ts_iter_active = true;
        if (cfg->by_time)
        {
            fdb_tsl_iter_by_time(inst->u.ts,
                                 (fdb_time_t)cfg->from,
                                 (fdb_time_t)cfg->to,
                                 ts_collect_cb, f);
        }
        else if (cfg->reverse)
        {
            fdb_tsl_iter_reverse(inst->u.ts, ts_collect_cb, f);
        }
        else
        {
            fdb_tsl_iter(inst->u.ts, ts_collect_cb, f);
        }
        return f->ts_iter_overflow ? POSIX_ERR_NOMEM : POSIX_OK;
    }
    case POSIX_FDB_TS_IOCTL_ITER_NEXT:
    {
        if (!f->ts_iter_active || arg == NULL)
        {
            return POSIX_ERR_INVAL;
        }
        posix_fdb_ts_entry_t *out = (posix_fdb_ts_entry_t *)arg;
        if (out->buf == NULL)
        {
            return POSIX_ERR_INVAL;
        }
        if (f->ts_cache_pos >= f->ts_cache_count)
        {
            f->ts_iter_active = false;
            return POSIX_ERR_NODEV;
        }
        struct fdb_tsl tsl;
        memset(&tsl, 0, sizeof(tsl));
        tsl.addr.index = f->ts_addr_cache[f->ts_cache_pos++];
        struct fdb_blob blob;
        fdb_blob_make(&blob, out->buf, out->buf_len);
        size_t got = fdb_blob_read((fdb_db_t)inst->u.ts,
                                   fdb_tsl_to_blob(&tsl, &blob));
        out->got    = got;
        out->ts     = (posix_fdb_time_t)tsl.time;
        out->status = (uint8_t)tsl.status;
        out->addr   = tsl.addr.index;
        return POSIX_OK;
    }
    case POSIX_FDB_TS_IOCTL_QUERY_COUNT:
    {
        if (arg == NULL)
        {
            return POSIX_ERR_INVAL;
        }
        posix_fdb_ts_count_t *q = (posix_fdb_ts_count_t *)arg;
        q->count = fdb_tsl_query_count(inst->u.ts,
                                       (fdb_time_t)q->from,
                                       (fdb_time_t)q->to,
                                       (fdb_tsl_status_t)q->status);
        return POSIX_OK;
    }
    case POSIX_FDB_TS_IOCTL_SET_STATUS:
    {
        if (arg == NULL)
        {
            return POSIX_ERR_INVAL;
        }
        posix_fdb_ts_set_status_t *s = (posix_fdb_ts_set_status_t *)arg;
        struct fdb_tsl tsl;
        memset(&tsl, 0, sizeof(tsl));
        tsl.addr.index = s->addr;
        fdb_err_t e = fdb_tsl_set_status(inst->u.ts, &tsl,
                                         (fdb_tsl_status_t)s->new_status);
        return fdb_err_to_posix(e);
    }
    case POSIX_FDB_TS_IOCTL_CLEAN:
    {
        fdb_tsl_clean(inst->u.ts);
        return POSIX_OK;
    }
    case POSIX_FDB_TS_IOCTL_GET_LAST_TIME:
    {
        if (arg == NULL)
        {
            return POSIX_ERR_INVAL;
        }
        fdb_time_t t = 0;
        fdb_tsdb_control(inst->u.ts, FDB_TSDB_CTRL_GET_LAST_TIME, &t);
        *(posix_fdb_time_t *)arg = (posix_fdb_time_t)t;
        return POSIX_OK;
    }
    default:
        return POSIX_ERR_NOSUPP;
    }
}

posix_ssize_t ts_write(fdb_inst_t *inst, const void *buf, size_t count)
{
    if (buf == NULL || count == 0)
    {
        return POSIX_ERR_INVAL;
    }
    struct fdb_blob blob;
    fdb_blob_make(&blob, (void *)buf, count);
    fdb_err_t e = fdb_tsl_append(inst->u.ts, &blob);
    if (e != FDB_NO_ERR)
    {
        return (posix_ssize_t)fdb_err_to_posix(e);
    }
    return (posix_ssize_t)count;
}

/* Application supplies the time source. If absent, the weak fallback below
 * returns 0 so TSDB still works but all records share the same timestamp. */
extern fdb_time_t posix_fdb_ts_get_time(void) __attribute__((weak));
fdb_time_t posix_fdb_ts_get_time(void) { return 0; }

static struct fdb_tsdb s_tsdb_log;
static fdb_inst_t s_inst_tsdb_log =
{
    .subtype = POSIX_FDB_SUBTYPE_TS,
    .path    = "/dev/fdb/ts/log",
    .name    = "log",
    .u.ts    = &s_tsdb_log,
};

static int fdb_ts_init(void)
{
    if (fdb_tsdb_init(&s_tsdb_log, "log", "fdb_tsdb1",
                      posix_fdb_ts_get_time, 128, NULL) != FDB_NO_ERR)
    {
        return POSIX_ERR_IO;
    }
    return posix_device_register(s_inst_tsdb_log.path, &g_fdb_ops, &s_inst_tsdb_log);
}
POSIX_INIT_DEVICE_EXPORT(fdb_ts_init);

#endif /* FDB_USING_TSDB */
