#ifndef POSIX_PORT_FDB_PRIV_H
#define POSIX_PORT_FDB_PRIV_H

#include "posix.h"
#include "ioctls/posix_ioctl_fdb.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <flashdb.h>

#ifndef POSIX_FDB_FILE_POOL_SIZE
#define POSIX_FDB_FILE_POOL_SIZE        4
#endif

#ifndef CONFIG_POSIX_FDB_TS_ITER_CACHE_MAX
#define CONFIG_POSIX_FDB_TS_ITER_CACHE_MAX  128
#endif

#ifndef CONFIG_POSIX_FDB_BF_FOREACH_CACHE_MAX
#define CONFIG_POSIX_FDB_BF_FOREACH_CACHE_MAX  32
#endif

typedef struct {
    uint8_t      subtype;
    const char  *path;
    const char  *name;
    union {
        fdb_kvdb_t  kv;
        fdb_tsdb_t  ts;
#ifdef FDB_USING_BF
        fdb_bf_t    bf;
#endif
    } u;
} fdb_inst_t;

typedef struct {
    bool            in_use;
    fdb_inst_t     *inst;

    struct fdb_kv_iterator  kv_iter;
    bool                    kv_iter_active;

    uint32_t        ts_addr_cache[CONFIG_POSIX_FDB_TS_ITER_CACHE_MAX];
    uint16_t        ts_cache_count;
    uint16_t        ts_cache_pos;
    bool            ts_iter_active;
    bool            ts_iter_overflow;

#ifdef FDB_USING_BF
    fdb_bf_file_t   bf_write_file;

    bool            bf_read_active;
    uint32_t        bf_read_xip;
    size_t          bf_read_size;
    size_t          bf_read_pos;

    posix_fdb_bf_entry_t bf_foreach[CONFIG_POSIX_FDB_BF_FOREACH_CACHE_MAX];
    uint16_t        bf_foreach_count;
    uint16_t        bf_foreach_pos;
    bool            bf_foreach_active;
    bool            bf_foreach_overflow;
#endif
} fdb_file_t;

int fdb_err_to_posix(fdb_err_t e);

extern const posix_driver_ops_t g_fdb_ops;

#ifdef FDB_USING_KVDB
int kv_ioctl(fdb_inst_t *inst, fdb_file_t *f, unsigned long cmd, void *arg);
#endif

#ifdef FDB_USING_TSDB
int           ts_ioctl(fdb_inst_t *inst, fdb_file_t *f, unsigned long cmd, void *arg);
posix_ssize_t ts_write(fdb_inst_t *inst, const void *buf, size_t count);
#endif

#ifdef FDB_USING_BF
int           bf_ioctl(fdb_inst_t *inst, fdb_file_t *f, unsigned long cmd, void *arg);
posix_ssize_t bf_read(fdb_file_t *f, void *buf, size_t count);
posix_ssize_t bf_write(fdb_file_t *f, const void *buf, size_t count);
#endif

#endif
