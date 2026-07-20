#include "posix_port_fdb_priv.h"
#include <string.h>

/* ================================================================
 * FlashDB (KV/TS/BF) POSIX driver - shared core
 *
 * Holds the resources/dispatch shared by all three subtype drivers:
 *   - fdb_file_t pool (a single fd pool serves KV/TS/BF mixed-use)
 *   - error code translation
 *   - top-level open/close/read/write/ioctl with subtype dispatch
 *   - the single g_fdb_ops table passed to posix_device_register
 *     in every per-subtype init function
 *
 * Subtype-specific bodies live in posix_port_fdb_{kv,ts,bf}.c and are
 * declared extern in posix_port_fdb_priv.h.
 * ================================================================ */

static fdb_file_t s_files[POSIX_FDB_FILE_POOL_SIZE];

int fdb_err_to_posix(fdb_err_t e)
{
    switch (e)
    {
    case FDB_NO_ERR:         return POSIX_OK;
    case FDB_NOT_FOUND:      return POSIX_ERR_NODEV;
    case FDB_NO_SPACE:
    case FDB_SAVED_FULL:     return POSIX_ERR_NOMEM;
    case FDB_BUSY:           return POSIX_ERR_BUSY;
    case FDB_INVALID_PARAM:
    case FDB_KV_NAME_ERR:    return POSIX_ERR_INVAL;
    case FDB_UNSUPPORTED:    return POSIX_ERR_NOSUPP;
    case FDB_PART_NOT_FOUND:
    case FDB_INIT_FAILED:    return POSIX_ERR_NODEV;
    case FDB_ERASE_ERR:
    case FDB_READ_ERR:
    case FDB_WRITE_ERR:
    default:                 return POSIX_ERR_IO;
    }
}

static fdb_file_t *alloc_file(void)
{
    for (int i = 0; i < POSIX_FDB_FILE_POOL_SIZE; i++)
    {
        if (!s_files[i].in_use)
        {
            memset(&s_files[i], 0, sizeof(s_files[i]));
            s_files[i].in_use = true;
            return &s_files[i];
        }
    }
    return NULL;
}

static void free_file(fdb_file_t *f)
{
    if (f == NULL)
    {
        return;
    }
    f->in_use = false;
}

static void *fdb_open(void *drv_data, const char *path)
{
    (void)path;
    fdb_inst_t *inst = (fdb_inst_t *)drv_data;
    if (inst == NULL)
    {
        return POSIX_OPEN_ERR;
    }
    posix_lock();
    fdb_file_t *f = alloc_file();
    posix_unlock();
    if (f == NULL)
    {
        return POSIX_OPEN_ERR;
    }
    f->inst = inst;
    return f;
}

static int fdb_close(void *drv_data, void *file_priv)
{
    (void)drv_data;
    fdb_file_t *f = (fdb_file_t *)file_priv;
    if (f == NULL)
    {
        return POSIX_OK;
    }
    posix_lock();
#ifdef FDB_USING_BF
    if (f->bf_write_file != NULL)
    {
        fdb_bf_abort(f->bf_write_file);
        f->bf_write_file = NULL;
    }
#endif
    free_file(f);
    posix_unlock();
    return POSIX_OK;
}

static int fdb_ioctl(void *drv_data, void *file_priv, unsigned long cmd, void *arg)
{
    if (posix_port_in_isr())
    {
        return POSIX_ERR_ISR;
    }
    fdb_inst_t *inst = (fdb_inst_t *)drv_data;
    fdb_file_t *f    = (fdb_file_t *)file_priv;
    if (inst == NULL || f == NULL)
    {
        return POSIX_ERR_INVAL;
    }

    posix_lock();

    /* Cross-subtype check: only 0x0 (generic) commands are allowed
     * on any instance; 0x1/0x2/0x3 must match the instance's subtype. */
    uint8_t want = POSIX_FDB_NR_SUBTYPE(POSIX_IOC_NR(cmd));
    if (want != 0x0 && want != inst->subtype)
    {
        posix_unlock();
        return POSIX_ERR_INVAL;
    }

    int ret;
    if (want == 0x0)
    {
        switch (cmd)
        {
        case POSIX_FDB_IOCTL_GET_SUBTYPE:
            if (arg == NULL)
            {
                ret = POSIX_ERR_INVAL;
                break;
            }
            *(uint8_t *)arg = inst->subtype;
            ret = POSIX_OK;
            break;
        case POSIX_FDB_IOCTL_GET_NAME:
            if (arg == NULL)
            {
                ret = POSIX_ERR_INVAL;
                break;
            }
            *(const char **)arg = inst->name;
            ret = POSIX_OK;
            break;
        default:
            ret = POSIX_ERR_NOSUPP;
            break;
        }
        posix_unlock();
        return ret;
    }

    switch (inst->subtype)
    {
#ifdef FDB_USING_KVDB
    case POSIX_FDB_SUBTYPE_KV:
        ret = kv_ioctl(inst, f, cmd, arg);
        break;
#endif
#ifdef FDB_USING_TSDB
    case POSIX_FDB_SUBTYPE_TS:
        ret = ts_ioctl(inst, f, cmd, arg);
        break;
#endif
#ifdef FDB_USING_BF
    case POSIX_FDB_SUBTYPE_BF:
        ret = bf_ioctl(inst, f, cmd, arg);
        break;
#endif
    default:
        ret = POSIX_ERR_NOSUPP;
        break;
    }
    posix_unlock();
    return ret;
}

static posix_ssize_t fdb_read(void *drv_data, void *file_priv,
                              void *buf, size_t count)
{
    if (posix_port_in_isr())
    {
        return POSIX_ERR_ISR;
    }
    fdb_inst_t *inst = (fdb_inst_t *)drv_data;
    fdb_file_t *f    = (fdb_file_t *)file_priv;
    if (inst == NULL || f == NULL)
    {
        return POSIX_ERR_INVAL;
    }
    posix_lock();
    posix_ssize_t ret;
    switch (inst->subtype)
    {
#ifdef FDB_USING_BF
    case POSIX_FDB_SUBTYPE_BF:
        ret = bf_read(f, buf, count);
        break;
#endif
    default:
        ret = POSIX_ERR_NOSUPP;
        break;
    }
    posix_unlock();
    return ret;
}

static posix_ssize_t fdb_write(void *drv_data, void *file_priv,
                               const void *buf, size_t count)
{
    if (posix_port_in_isr())
    {
        return POSIX_ERR_ISR;
    }
    fdb_inst_t *inst = (fdb_inst_t *)drv_data;
    fdb_file_t *f    = (fdb_file_t *)file_priv;
    if (inst == NULL || f == NULL)
    {
        return POSIX_ERR_INVAL;
    }
    posix_lock();
    posix_ssize_t ret;
    switch (inst->subtype)
    {
#ifdef FDB_USING_TSDB
    case POSIX_FDB_SUBTYPE_TS:
        (void)f;
        ret = ts_write(inst, buf, count);
        break;
#endif
#ifdef FDB_USING_BF
    case POSIX_FDB_SUBTYPE_BF:
        ret = bf_write(f, buf, count);
        break;
#endif
    default:
        ret = POSIX_ERR_NOSUPP;
        break;
    }
    posix_unlock();
    return ret;
}

const posix_driver_ops_t g_fdb_ops =
{
    .open  = fdb_open,
    .close = fdb_close,
    .read  = fdb_read,
    .write = fdb_write,
    .ioctl = fdb_ioctl,
};
