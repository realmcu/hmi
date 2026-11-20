/* ================================================================
 * POSIX device abstraction layer — core implementation
 *
 * Responsibilities:
 * 1. Device table management (register/unregister/lookup, with mount-point prefix matching)
 * 2. posix_open → look up device table → take a reference → call ops->open → store file_priv
 * 3. posix_read/write/ioctl/close → validate fd → take a snapshot → call ops outside the lock
 * 4. ISR variants skip the lock and call ops directly after only a handle validity check
 *
 * === Thread-safety strategy (important) ===
 *   The framework lock protects only framework metadata like "device table / fd pool / ref counts".
 *   Each API, under the lock, does: handle validity check + device lookup + take an
 *   (ops pointer, drv_data, file_priv) snapshot, then [releases the lock] before calling
 *   the driver ops callback. Therefore:
 *     - driver ops may safely block/sleep and will not serialize other devices or hang the system.
 *     - an open fd makes the device's ref_count > 0, preventing it from being unregistered
 *       and guaranteeing the device entry and drv_data survive while ops run outside the lock.
 *   posix_*_isr cannot lock (mutexes are unusable in ISRs); they validate then call ops directly;
 *   the caller must ensure devices accessed from the ISR are not unregistered concurrently.
 *
 * Porting to a new platform requires no change to this file,
 * only implementing posix_lock/posix_unlock/posix_port_in_isr (weak default implementations).
 * ================================================================ */

#include "posix_device.h"
#include <stdio.h>
#include <string.h>

/* ================================================================
 * Internal data structures
 * ================================================================ */

#ifndef POSIX_DEVICE_TABLE_SIZE
#define POSIX_DEVICE_TABLE_SIZE  32
#endif

#ifndef POSIX_FD_POOL_SIZE
#define POSIX_FD_POOL_SIZE       16   /* max number of simultaneously open files */
#endif

/* fd validity magic ('pxfd'), used to detect wild pointers/stale handles from a closed fd */
#define POSIX_FD_MAGIC  0x70786664u

/* Device table entry */
typedef struct
{
    char                     path[32];
    const posix_driver_ops_t *ops;
    void                     *drv_data;       /* driver private data */
    int                      ref_count;
    int                      in_use;
} posix_device_entry_t;

/* fd struct — allocated from the pool on open */
struct posix_device
{
    uint32_t              magic;      /* == POSIX_FD_MAGIC means valid */
    posix_device_entry_t *entry;      /* points into the device table */
    void                 *file_priv;  /* per-open private data (ops->open return value) */
    int                   in_use;     /* whether this fd is in use */
};

/* ---------- Static resources ---------- */
static posix_device_entry_t s_dev_table[POSIX_DEVICE_TABLE_SIZE];
static struct posix_device s_fd_pool[POSIX_FD_POOL_SIZE];

/* ================================================================
 * Porting layer weak-symbol default implementations (port overrides with strong symbols)
 * ================================================================ */

void posix_lock(void)   __attribute__((weak));
void posix_unlock(void) __attribute__((weak));
void posix_lock(void)   { /* empty by default, user overrides */ }
void posix_unlock(void) { /* empty by default, user overrides */ }

int posix_port_in_isr(void) __attribute__((weak));
int posix_port_in_isr(void) { return 0; }

/* ================================================================
 * Internal helper functions
 * ================================================================ */

/* ---- Device table operations ---- */
static posix_device_entry_t *entry_by_path(const char *path)
{
    for (int i = 0; i < POSIX_DEVICE_TABLE_SIZE; i++)
    {
        if (s_dev_table[i].in_use &&
            strcmp(s_dev_table[i].path, path) == 0)
        {
            return &s_dev_table[i];
        }
    }
    return NULL;
}

/* Lookup for open: exact match first; on failure do mount-point prefix matching —
 * the registered path is a prefix of query and is immediately followed by '/' (longest match wins).
 * This lets a registered "/dev/gpio0" serve pin-level paths like "/dev/gpio0/p12";
 * the full path is passed to ops->open and the driver parses the remaining subpath. */
static posix_device_entry_t *entry_for_open(const char *path)
{
    posix_device_entry_t *exact = entry_by_path(path);
    if (exact) { return exact; }

    posix_device_entry_t *best = NULL;
    size_t best_len = 0;
    for (int i = 0; i < POSIX_DEVICE_TABLE_SIZE; i++)
    {
        if (!s_dev_table[i].in_use) { continue; }
        size_t plen = strlen(s_dev_table[i].path);
        if (plen > best_len &&
            strncmp(s_dev_table[i].path, path, plen) == 0 &&
            path[plen] == '/')
        {
            best     = &s_dev_table[i];
            best_len = plen;
        }
    }
    return best;
}

static posix_device_entry_t *free_entry(void)
{
    for (int i = 0; i < POSIX_DEVICE_TABLE_SIZE; i++)
    {
        if (!s_dev_table[i].in_use) { return &s_dev_table[i]; }
    }
    return NULL;
}

/* ---- fd pool operations ---- */
static struct posix_device *alloc_fd(void)
{
    for (int i = 0; i < POSIX_FD_POOL_SIZE; i++)
    {
        if (!s_fd_pool[i].in_use)
        {
            s_fd_pool[i].magic     = POSIX_FD_MAGIC;
            s_fd_pool[i].in_use    = 1;
            s_fd_pool[i].entry     = NULL;
            s_fd_pool[i].file_priv = NULL;
            return &s_fd_pool[i];
        }
    }
    return NULL;
}

static void free_fd(struct posix_device *fd)
{
    fd->magic     = 0;
    fd->in_use    = 0;
    fd->entry     = NULL;
    fd->file_priv = NULL;
}

/* Validate an fd handle: must fall inside the fd pool, be aligned, have the right magic, and be in use.
 * Catches wild pointers, stack addresses, and stale handles reused after close.
 * The caller decides whether to call this while holding the lock. */
static int fd_is_valid(const struct posix_device *f)
{
    if (f < &s_fd_pool[0] || f >= &s_fd_pool[POSIX_FD_POOL_SIZE])
    {
        return 0;
    }
    if ((size_t)((const char *)f - (const char *)&s_fd_pool[0])
        % sizeof(s_fd_pool[0]) != 0)
    {
        return 0;
    }
    if (f->magic != POSIX_FD_MAGIC || !f->in_use)
    {
        return 0;
    }
    return 1;
}

/* ================================================================
 * Device registration API
 * ================================================================ */

int posix_device_register(const char *path,
                          const posix_driver_ops_t *ops,
                          void *drv_data)
{
    if (!path || !ops) { return POSIX_ERR_INVAL; }

    posix_lock();

    if (entry_by_path(path))
    {
        posix_unlock();
        return POSIX_ERR_BUSY;
    }

    posix_device_entry_t *e = free_entry();
    if (!e) { posix_unlock(); return POSIX_ERR_NOMEM; }

    size_t len = strlen(path);
    if (len >= sizeof(e->path)) { posix_unlock(); return POSIX_ERR_INVAL; }

    strcpy(e->path, path);
    e->ops      = ops;
    e->drv_data = drv_data;
    e->ref_count = 0;
    e->in_use   = 1;

    posix_unlock();
    return POSIX_OK;
}

int posix_device_register_group(const char *fmt, int count,
                                const posix_driver_ops_t *ops,
                                void **privs)
{
    int done = 0;   /* number of successfully registered items; rollback undoes only these */
    int err  = POSIX_ERR_NOMEM;

    for (int i = 0; i < count; i++)
    {
        char path[32];
        int n = snprintf(path, sizeof(path), fmt, i);
        if (n < 0 || (size_t)n >= sizeof(path)) { err = POSIX_ERR_INVAL; goto rollback; }
        int ret = posix_device_register(path, ops,
                                        privs ? privs[i] : NULL);
        if (ret != POSIX_OK) { err = ret; goto rollback; }
        done++;
    }
    return POSIX_OK;

rollback:
    for (int i = 0; i < done; i++)
    {
        char path[32];
        snprintf(path, sizeof(path), fmt, i);
        posix_device_unregister(path);
    }
    return err;
}

int posix_device_unregister(const char *path)
{
    if (!path) { return POSIX_ERR_INVAL; }

    posix_lock();
    posix_device_entry_t *e = entry_by_path(path);
    if (!e)  { posix_unlock(); return POSIX_ERR_NODEV; }
    if (e->ref_count > 0) { posix_unlock(); return POSIX_ERR_BUSY; }
    memset(e, 0, sizeof(*e));
    posix_unlock();
    return POSIX_OK;
}

/* Read-only lookup: copy a registered entry's ops+drv_data to the caller.
 * Used to alias by registered path (e.g. the touch layer mounting /dev/cst816d again as /dev/touch0).
 * Does not touch ref_count; the caller should immediately create the alias with posix_device_register,
 * and the alias's ref_count is tracked independently. */
int posix_device_lookup(const char *path,
                        const posix_driver_ops_t **ops_out,
                        void **drv_out)
{
    if (!path) { return POSIX_ERR_INVAL; }

    posix_lock();
    posix_device_entry_t *e = entry_by_path(path);
    if (!e) { posix_unlock(); return POSIX_ERR_NODEV; }

    if (ops_out) { *ops_out = e->ops; }
    if (drv_out) { *drv_out = e->drv_data; }
    posix_unlock();
    return POSIX_OK;
}

/* Alias registration: mount src_path's registered ops+drv_data again under the alias name.
 * Generic for touch/gsensor "multiple models + current board selection" scenarios, replacing
 * copy-pasted per-subsystem bind APIs.
 *
 * Note: alias and src_path are two independent entries, each with its own ref_count. Opening
 * both paths takes two fd slots (and two slots in each chip's own per-open pool). In the vast majority
 * of scenarios the app opens only one of alias or src_path. */
int posix_device_bind_alias(const char *alias, const char *src_path)
{
    if (!alias || !src_path) { return POSIX_ERR_INVAL; }

    const posix_driver_ops_t *ops = NULL;
    void                     *drv = NULL;
    int ret = posix_device_lookup(src_path, &ops, &drv);
    if (ret != POSIX_OK) { return ret; }
    if (!ops)            { return POSIX_ERR_NODEV; }

    return posix_device_register(alias, ops, drv);
}

int posix_device_unbind_alias(const char *alias)
{
    if (!alias) { return POSIX_ERR_INVAL; }
    return posix_device_unregister(alias);
}

/* ================================================================
 * POSIX user API
 *
 * Uniform pattern: validate + take snapshot under the lock, call ops outside it.
 * ================================================================ */

posix_fd_t posix_open(const char *path)
{
    if (!path) { return POSIX_FD_NULL; }

    posix_lock();

    posix_device_entry_t *e = entry_for_open(path);
    if (!e) { posix_unlock(); return POSIX_FD_NULL; }

    struct posix_device *fd = alloc_fd();
    if (!fd) { posix_unlock(); return POSIX_FD_NULL; }

    fd->entry = e;
    e->ref_count++;     /* take the reference first so the device is not unregistered while open runs outside the lock */

    const posix_driver_ops_t *ops = e->ops;
    void                     *drv = e->drv_data;
    posix_unlock();

    /* call driver open outside the lock (may block) */
    void *fp;
    if (ops && ops->open)
    {
        fp = ops->open(drv, path);
    }
    else
    {
        fp = drv;   /* reuse drv_data when there is no open callback */
    }

    if (fp == POSIX_OPEN_ERR)
    {
        /* driver open failed: roll back the reference and fd */
        posix_lock();
        if (e->ref_count > 0) { e->ref_count--; }
        free_fd(fd);
        posix_unlock();
        return POSIX_FD_NULL;
    }

    posix_lock();
    fd->file_priv = fp;
    posix_unlock();
    return (posix_fd_t)fd;
}

int posix_close(posix_fd_t fd)
{
    if (!fd) { return POSIX_ERR_INVAL; }
    struct posix_device *f = (struct posix_device *)fd;

    posix_lock();
    if (!fd_is_valid(f)) { posix_unlock(); return POSIX_ERR_INVAL; }

    posix_device_entry_t     *e   = f->entry;
    const posix_driver_ops_t *ops = e ? e->ops : NULL;
    void                     *drv = e ? e->drv_data : NULL;
    void                     *fp  = f->file_priv;

    /* invalidate the fd immediately so a concurrent/repeated close cannot hit the same handle */
    free_fd(f);
    if (e && e->ref_count > 0) { e->ref_count--; }
    posix_unlock();

    /* call driver close outside the lock (may block); ops points to a static table and drv/fp are snapshots, both safe */
    int ret = POSIX_OK;
    if (ops && ops->close)
    {
        ret = ops->close(drv, fp);
    }
    return ret;
}

posix_ssize_t posix_read(posix_fd_t fd, void *buf, size_t count)
{
    if (!fd || !buf || !count) { return POSIX_ERR_INVAL; }
    struct posix_device *f = (struct posix_device *)fd;

    posix_lock();
    if (!fd_is_valid(f)) { posix_unlock(); return POSIX_ERR_INVAL; }
    posix_device_entry_t *e = f->entry;
    if (!e || !e->in_use || !e->ops || !e->ops->read)
    {
        posix_unlock();
        return POSIX_ERR_NODEV;
    }
    posix_ssize_t (*read_fn)(void *, void *, void *, size_t) = e->ops->read;
    void *drv = e->drv_data;
    void *fp  = f->file_priv;
    posix_unlock();

    return read_fn(drv, fp, buf, count);
}

posix_ssize_t posix_write(posix_fd_t fd, const void *buf, size_t count)
{
    if (!fd || !buf || !count) { return POSIX_ERR_INVAL; }
    struct posix_device *f = (struct posix_device *)fd;

    posix_lock();
    if (!fd_is_valid(f)) { posix_unlock(); return POSIX_ERR_INVAL; }
    posix_device_entry_t *e = f->entry;
    if (!e || !e->in_use || !e->ops || !e->ops->write)
    {
        posix_unlock();
        return POSIX_ERR_NODEV;
    }
    posix_ssize_t (*write_fn)(void *, void *, const void *, size_t) = e->ops->write;
    void *drv = e->drv_data;
    void *fp  = f->file_priv;
    posix_unlock();

    return write_fn(drv, fp, buf, count);
}

int posix_ioctl(posix_fd_t fd, unsigned long cmd, void *arg)
{
    if (!fd) { return POSIX_ERR_INVAL; }
    struct posix_device *f = (struct posix_device *)fd;

    posix_lock();
    if (!fd_is_valid(f)) { posix_unlock(); return POSIX_ERR_INVAL; }
    posix_device_entry_t *e = f->entry;
    if (!e || !e->in_use || !e->ops || !e->ops->ioctl)
    {
        posix_unlock();
        return POSIX_ERR_NODEV;
    }
    int (*ioctl_fn)(void *, void *, unsigned long, void *) = e->ops->ioctl;
    void *drv = e->drv_data;
    void *fp  = f->file_priv;
    posix_unlock();

    return ioctl_fn(drv, fp, cmd, arg);
}

/* ================================================================
 * ISR-safe variants (skip the lock, call driver ops directly)
 *
 * No locking (RTOS mutexes are unusable in ISR context), only a handle validity check.
 * No longer modifies cmd — drivers uniformly use posix_port_in_isr() to determine context,
 * avoiding polluting the cmd namespace.
 * The caller must ensure devices accessed from the ISR are not unregistered concurrently.
 * ================================================================ */

posix_ssize_t posix_read_isr(posix_fd_t fd, void *buf, size_t count)
{
    if (!fd || !buf || !count) { return POSIX_ERR_INVAL; }
    struct posix_device *f = (struct posix_device *)fd;
    if (!fd_is_valid(f)) { return POSIX_ERR_INVAL; }
    posix_device_entry_t *e = f->entry;
    if (!e || !e->in_use || !e->ops || !e->ops->read) { return POSIX_ERR_NODEV; }
    return e->ops->read(e->drv_data, f->file_priv, buf, count);
}

posix_ssize_t posix_write_isr(posix_fd_t fd, const void *buf, size_t count)
{
    if (!fd || !buf || !count) { return POSIX_ERR_INVAL; }
    struct posix_device *f = (struct posix_device *)fd;
    if (!fd_is_valid(f)) { return POSIX_ERR_INVAL; }
    posix_device_entry_t *e = f->entry;
    if (!e || !e->in_use || !e->ops || !e->ops->write) { return POSIX_ERR_NODEV; }
    return e->ops->write(e->drv_data, f->file_priv, buf, count);
}

int posix_ioctl_isr(posix_fd_t fd, unsigned long cmd, void *arg)
{
    if (!fd) { return POSIX_ERR_INVAL; }
    struct posix_device *f = (struct posix_device *)fd;
    if (!fd_is_valid(f)) { return POSIX_ERR_INVAL; }
    posix_device_entry_t *e = f->entry;
    if (!e || !e->in_use || !e->ops || !e->ops->ioctl) { return POSIX_ERR_NODEV; }
    return e->ops->ioctl(e->drv_data, f->file_priv, cmd, arg);
}
