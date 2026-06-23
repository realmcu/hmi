#include "posix_port_fdb_priv.h"
#include "posix_init.h"
#include <string.h>

/* ================================================================
 * KVDB + BF combined implementation.
 *
 * BF is parasitic on a KVDB (FlashDB stores BF directory entries as
 * KVs under FDB_BF_KEY_PREFIX). Co-locating them here:
 *   - keeps s_kvdb_env static (BF needs it as host_kvdb)
 *   - guarantees fdb_kvdb_init runs before fdb_bf_init within a
 *     single init function, instead of relying on link-order tricks
 *     across two compilation units.
 * ================================================================ */

#ifdef FDB_USING_KVDB

int kv_ioctl(fdb_inst_t *inst, fdb_file_t *f, unsigned long cmd, void *arg)
{
    switch (cmd)
    {
    case POSIX_FDB_KV_IOCTL_SET_BLOB:
    {
        if (arg == NULL)
        {
            return POSIX_ERR_INVAL;
        }
        posix_fdb_kv_io_t *io = (posix_fdb_kv_io_t *)arg;
        if (io->key == NULL || (io->buf == NULL && io->buf_len > 0))
        {
            return POSIX_ERR_INVAL;
        }
        struct fdb_blob blob;
        fdb_err_t e = fdb_kv_set_blob(inst->u.kv, io->key,
                                      fdb_blob_make(&blob, io->buf, io->buf_len));
        return fdb_err_to_posix(e);
    }
    case POSIX_FDB_KV_IOCTL_GET_BLOB:
    {
        if (arg == NULL)
        {
            return POSIX_ERR_INVAL;
        }
        posix_fdb_kv_io_t *io = (posix_fdb_kv_io_t *)arg;
        if (io->key == NULL || io->buf == NULL)
        {
            return POSIX_ERR_INVAL;
        }
        struct fdb_blob blob;
        size_t got = fdb_kv_get_blob(inst->u.kv, io->key,
                                     fdb_blob_make(&blob, io->buf, io->buf_len));
        io->got = blob.saved.len;
        if (got == 0 && blob.saved.len == 0)
        {
            return POSIX_ERR_NODEV;
        }
        return POSIX_OK;
    }
    case POSIX_FDB_KV_IOCTL_SET_STR:
    {
        if (arg == NULL)
        {
            return POSIX_ERR_INVAL;
        }
        posix_fdb_kv_str_t *io = (posix_fdb_kv_str_t *)arg;
        if (io->key == NULL || io->buf == NULL)
        {
            return POSIX_ERR_INVAL;
        }
        fdb_err_t e = fdb_kv_set(inst->u.kv, io->key, io->buf);
        return fdb_err_to_posix(e);
    }
    case POSIX_FDB_KV_IOCTL_GET_STR:
    {
        if (arg == NULL)
        {
            return POSIX_ERR_INVAL;
        }
        posix_fdb_kv_str_t *io = (posix_fdb_kv_str_t *)arg;
        if (io->key == NULL || io->buf == NULL || io->buf_len == 0)
        {
            return POSIX_ERR_INVAL;
        }
        char *s = fdb_kv_get(inst->u.kv, io->key);
        if (s == NULL)
        {
            io->got = 0;
            return POSIX_ERR_NODEV;
        }
        size_t need = strlen(s);
        size_t copy = (need + 1 <= io->buf_len) ? need : (io->buf_len - 1);
        memcpy(io->buf, s, copy);
        io->buf[copy] = '\0';
        io->got = copy;
        return (need + 1 <= io->buf_len) ? POSIX_OK : POSIX_ERR_NOMEM;
    }
    case POSIX_FDB_KV_IOCTL_DEL:
    {
        if (arg == NULL)
        {
            return POSIX_ERR_INVAL;
        }
        fdb_err_t e = fdb_kv_del(inst->u.kv, (const char *)arg);
        return fdb_err_to_posix(e);
    }
    case POSIX_FDB_KV_IOCTL_EXISTS:
    {
        if (arg == NULL)
        {
            return POSIX_ERR_INVAL;
        }
        posix_fdb_kv_exists_t *q = (posix_fdb_kv_exists_t *)arg;
        if (q->key == NULL)
        {
            return POSIX_ERR_INVAL;
        }
        struct fdb_kv kv;
        q->exists = (fdb_kv_get_obj(inst->u.kv, q->key, &kv) != NULL);
        return POSIX_OK;
    }
    case POSIX_FDB_KV_IOCTL_ITER_INIT:
    {
        fdb_kv_iterator_init(inst->u.kv, &f->kv_iter);
        f->kv_iter_active = true;
        return POSIX_OK;
    }
    case POSIX_FDB_KV_IOCTL_ITER_NEXT:
    {
        if (!f->kv_iter_active || arg == NULL)
        {
            return POSIX_ERR_INVAL;
        }
        posix_fdb_kv_entry_t *out = (posix_fdb_kv_entry_t *)arg;
        if (!fdb_kv_iterate(inst->u.kv, &f->kv_iter))
        {
            f->kv_iter_active = false;
            return POSIX_ERR_NODEV;
        }
        size_t n = f->kv_iter.curr_kv.name_len;
        if (n >= sizeof(out->name))
        {
            n = sizeof(out->name) - 1;
        }
        memcpy(out->name, f->kv_iter.curr_kv.name, n);
        out->name[n] = '\0';
        out->value_len = f->kv_iter.curr_kv.value_len;
        return POSIX_OK;
    }
    case POSIX_FDB_KV_IOCTL_RESET:
    {
        fdb_err_t e = fdb_kv_set_default(inst->u.kv);
        return fdb_err_to_posix(e);
    }
    default:
        return POSIX_ERR_NOSUPP;
    }
}

#endif /* FDB_USING_KVDB */

#ifdef FDB_USING_BF

static bool bf_collect_cb(const char *key, const struct fdb_bf_dirent *ent,
                          uint32_t xip_addr, void *arg)
{
    fdb_file_t *f = (fdb_file_t *)arg;
    if (f->bf_foreach_count >= CONFIG_POSIX_FDB_BF_FOREACH_CACHE_MAX)
    {
        f->bf_foreach_overflow = true;
        return true;
    }
    posix_fdb_bf_entry_t *e = &f->bf_foreach[f->bf_foreach_count++];
    size_t n = strlen(key);
    if (n >= sizeof(e->key))
    {
        n = sizeof(e->key) - 1;
    }
    memcpy(e->key, key, n);
    e->key[n] = '\0';
    e->ent.offset   = ent->offset;
    e->ent.capacity = ent->capacity;
    e->ent.size     = ent->size;
    e->ent.data_crc = ent->data_crc;
    e->ent.flags    = ent->flags;
    e->xip_addr     = xip_addr;
    return false;
}

int bf_ioctl(fdb_inst_t *inst, fdb_file_t *f, unsigned long cmd, void *arg)
{
    switch (cmd)
    {
    case POSIX_FDB_BF_IOCTL_CREATE:
    {
        if (arg == NULL)
        {
            return POSIX_ERR_INVAL;
        }
        if (f->bf_write_file != NULL)
        {
            return POSIX_ERR_BUSY;
        }
        posix_fdb_bf_create_t *c = (posix_fdb_bf_create_t *)arg;
        if (c->key == NULL)
        {
            return POSIX_ERR_INVAL;
        }
        fdb_err_t e = fdb_bf_create(inst->u.bf, c->key, c->max_size, &f->bf_write_file);
        return fdb_err_to_posix(e);
    }
    case POSIX_FDB_BF_IOCTL_COMMIT:
    {
        if (f->bf_write_file == NULL)
        {
            return POSIX_ERR_INVAL;
        }
        fdb_err_t e = fdb_bf_commit(f->bf_write_file, (const uint32_t *)arg);
        f->bf_write_file = NULL;
        return fdb_err_to_posix(e);
    }
    case POSIX_FDB_BF_IOCTL_ABORT:
    {
        if (f->bf_write_file == NULL)
        {
            return POSIX_ERR_INVAL;
        }
        fdb_err_t e = fdb_bf_abort(f->bf_write_file);
        f->bf_write_file = NULL;
        return fdb_err_to_posix(e);
    }
    case POSIX_FDB_BF_IOCTL_DELETE:
    {
        if (arg == NULL)
        {
            return POSIX_ERR_INVAL;
        }
        fdb_err_t e = fdb_bf_delete(inst->u.bf, (const char *)arg);
        return fdb_err_to_posix(e);
    }
    case POSIX_FDB_BF_IOCTL_DELETE_BY_ADDR:
    {
        if (arg == NULL)
        {
            return POSIX_ERR_INVAL;
        }
        fdb_err_t e = fdb_bf_delete_by_addr(inst->u.bf, *(uint32_t *)arg);
        return fdb_err_to_posix(e);
    }
    case POSIX_FDB_BF_IOCTL_STAT:
    {
        if (arg == NULL)
        {
            return POSIX_ERR_INVAL;
        }
        posix_fdb_bf_stat_t *s = (posix_fdb_bf_stat_t *)arg;
        if (s->key == NULL)
        {
            return POSIX_ERR_INVAL;
        }
        struct fdb_bf_dirent ent;
        fdb_err_t e = fdb_bf_stat(inst->u.bf, s->key, &ent);
        if (e == FDB_NO_ERR)
        {
            s->ent.offset   = ent.offset;
            s->ent.capacity = ent.capacity;
            s->ent.size     = ent.size;
            s->ent.data_crc = ent.data_crc;
            s->ent.flags    = ent.flags;
        }
        return fdb_err_to_posix(e);
    }
    case POSIX_FDB_BF_IOCTL_EXISTS:
    {
        if (arg == NULL)
        {
            return POSIX_ERR_INVAL;
        }
        posix_fdb_bf_exists_t *q = (posix_fdb_bf_exists_t *)arg;
        if (q->key == NULL)
        {
            return POSIX_ERR_INVAL;
        }
        q->exists = fdb_bf_exists(inst->u.bf, q->key);
        return POSIX_OK;
    }
    case POSIX_FDB_BF_IOCTL_GET_XIP:
    {
        if (arg == NULL)
        {
            return POSIX_ERR_INVAL;
        }
        posix_fdb_bf_xip_t *x = (posix_fdb_bf_xip_t *)arg;
        if (x->key == NULL)
        {
            return POSIX_ERR_INVAL;
        }
        uint32_t addr = 0;
        size_t   size = 0;
        fdb_err_t e = fdb_bf_get_addr(inst->u.bf, x->key, &addr, &size);
        if (e == FDB_NO_ERR)
        {
            x->xip_addr = addr;
            x->size     = size;
        }
        return fdb_err_to_posix(e);
    }
    case POSIX_FDB_BF_IOCTL_SELECT_READ:
    {
        if (arg == NULL)
        {
            return POSIX_ERR_INVAL;
        }
        const char *key = (const char *)arg;
        uint32_t addr = 0;
        size_t   size = 0;
        fdb_err_t e = fdb_bf_get_addr(inst->u.bf, key, &addr, &size);
        if (e != FDB_NO_ERR)
        {
            return fdb_err_to_posix(e);
        }
        f->bf_read_xip    = addr;
        f->bf_read_size   = size;
        f->bf_read_pos    = 0;
        f->bf_read_active = true;
        return POSIX_OK;
    }
    case POSIX_FDB_BF_IOCTL_FOREACH_INIT:
    {
        f->bf_foreach_count    = 0;
        f->bf_foreach_pos      = 0;
        f->bf_foreach_overflow = false;
        f->bf_foreach_active   = true;
        fdb_bf_foreach(inst->u.bf, bf_collect_cb, f);
        return f->bf_foreach_overflow ? POSIX_ERR_NOMEM : POSIX_OK;
    }
    case POSIX_FDB_BF_IOCTL_FOREACH_NEXT:
    {
        if (!f->bf_foreach_active || arg == NULL)
        {
            return POSIX_ERR_INVAL;
        }
        if (f->bf_foreach_pos >= f->bf_foreach_count)
        {
            f->bf_foreach_active = false;
            return POSIX_ERR_NODEV;
        }
        *(posix_fdb_bf_entry_t *)arg = f->bf_foreach[f->bf_foreach_pos++];
        return POSIX_OK;
    }
    default:
        return POSIX_ERR_NOSUPP;
    }
}

posix_ssize_t bf_write(fdb_file_t *f, const void *buf, size_t count)
{
    if (f->bf_write_file == NULL)
    {
        return POSIX_ERR_INVAL;
    }
    if (buf == NULL || count == 0)
    {
        return POSIX_ERR_INVAL;
    }
    fdb_err_t e = fdb_bf_append(f->bf_write_file, buf, count);
    if (e != FDB_NO_ERR)
    {
        return (posix_ssize_t)fdb_err_to_posix(e);
    }
    return (posix_ssize_t)count;
}

posix_ssize_t bf_read(fdb_file_t *f, void *buf, size_t count)
{
    if (!f->bf_read_active)
    {
        return POSIX_ERR_INVAL;
    }
    if (buf == NULL || count == 0)
    {
        return POSIX_ERR_INVAL;
    }
    if (f->bf_read_pos >= f->bf_read_size)
    {
        return 0;
    }
    size_t remain = f->bf_read_size - f->bf_read_pos;
    size_t n = (count < remain) ? count : remain;
    /* XIP read: data partition is memory-mapped NOR; memcpy from absolute
     * flash address is the canonical zero-copy path used by FlashDB BF. */
    memcpy(buf, (const void *)(uintptr_t)(f->bf_read_xip + f->bf_read_pos), n);
    f->bf_read_pos += n;
    return (posix_ssize_t)n;
}

#endif /* FDB_USING_BF */

#ifdef FDB_USING_KVDB
static struct fdb_kvdb s_kvdb_env;
static fdb_inst_t s_inst_kvdb_env =
{
    .subtype = POSIX_FDB_SUBTYPE_KV,
    .path    = "/dev/fdb/kv/env",
    .name    = "env",
    .u.kv    = &s_kvdb_env,
};
#endif

#ifdef FDB_USING_BF
static struct fdb_bf s_bf_firmware;
static fdb_inst_t s_inst_bf_firmware =
{
    .subtype = POSIX_FDB_SUBTYPE_BF,
    .path    = "/dev/fdb/bf/firmware",
    .name    = "firmware",
    .u.bf    = &s_bf_firmware,
};
#endif

static int fdb_kv_bf_init(void)
{
#ifdef FDB_USING_KVDB
    if (fdb_kvdb_init(&s_kvdb_env, "env", "kvdb", NULL, NULL) != FDB_NO_ERR)
    {
        return POSIX_ERR_IO;
    }
    if (posix_device_register(s_inst_kvdb_env.path, &g_fdb_ops, &s_inst_kvdb_env) != POSIX_OK)
    {
        return POSIX_ERR_IO;
    }
#endif

#ifdef FDB_USING_BF
    if (fdb_bf_init(&s_bf_firmware, &s_kvdb_env, "bf_data", NULL) != FDB_NO_ERR)
    {
        return POSIX_ERR_IO;
    }
    if (posix_device_register(s_inst_bf_firmware.path, &g_fdb_ops, &s_inst_bf_firmware) != POSIX_OK)
    {
        return POSIX_ERR_IO;
    }
#endif

    return POSIX_OK;
}
POSIX_INIT_DEVICE_EXPORT(fdb_kv_bf_init);
