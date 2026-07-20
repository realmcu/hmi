#ifndef POSIX_IOCTL_FDB_H
#define POSIX_IOCTL_FDB_H

#include "../posix_ioctl.h"
#include "../posix_device.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * FlashDB (KV/TS/BF) - POSIX 抽象层 ioctl 定义
 *
 * 设计：单一 magic + 路径分流。
 *   /dev/fdb/kv/<name>   -> KVDB 实例
 *   /dev/fdb/ts/<name>   -> TSDB 实例
 *   /dev/fdb/bf/<name>   -> BF 实例 (寄生在某个 KVDB 上)
 *
 * nr 编码：高 4 bit 是子类型 (0x1=KV / 0x2=TS / 0x3=BF / 0x0=通用)，
 *          低 4 bit 是命令号。给一个 fd 发"对方子类型"的命令会返回
 *          POSIX_ERR_INVAL，框架/驱动会做交叉校验。
 *
 * read/write 在不同子类型下语义不同（参见每节说明）：
 *   KV: read/write 均不支持（key 无法承载在裸字节流里），全部走 ioctl
 *   TS: write = auto-timestamp append；read 不支持（迭代用游标 ioctl）
 *   BF: write = append 到 BF_CREATE 后的 active 写句柄；
 *       read  = 从 BF_SELECT_READ 后的 active key 顺序读
 *
 * 错误码：所有命令返回 POSIX_OK (0) 或 POSIX_ERR_* 负值。
 * ISR：所有 FlashDB 操作都涉及 flash 擦写，ISR 上下文一律返回 POSIX_ERR_ISR。
 * ================================================================ */

/* ---------------- 子类型常量（GET_SUBTYPE 出参 / 内部校验） ---------------- */
#define POSIX_FDB_SUBTYPE_KV   0x1
#define POSIX_FDB_SUBTYPE_TS   0x2
#define POSIX_FDB_SUBTYPE_BF   0x3

/* ---------------- 命令号编码 ---------------- */
#define POSIX_FDB_NR_SUBTYPE(nr)  (((nr) >> 4) & 0xF)
#define POSIX_FDB_NR_OP(nr)       ((nr) & 0xF)
#define POSIX_FDB_NR(sub, op)     ((((sub) & 0xF) << 4) | ((op) & 0xF))

/* ================================================================
 * 通用命令 (subtype = 0x0)
 * ================================================================ */
#define POSIX_FDB_IOCTL_GET_SUBTYPE   POSIX_IOC(POSIX_DEVICE_MAGIC_FDB, POSIX_FDB_NR(0x0, 0x1))
#define POSIX_FDB_IOCTL_GET_NAME      POSIX_IOC(POSIX_DEVICE_MAGIC_FDB, POSIX_FDB_NR(0x0, 0x2))

/* ================================================================
 * KVDB (subtype = 0x1)
 *
 * 调用约定：
 *   - posix_fdb_kv_io_t       键值 blob 读写（二进制安全）
 *   - posix_fdb_kv_str_t      键值字符串读写（NUL 结尾）
 *   - posix_fdb_kv_exists_t   存在性查询
 *   - posix_fdb_kv_entry_t    迭代器 NEXT 出参
 *
 * 迭代器使用：
 *   posix_ioctl(fd, KV_ITER_INIT, NULL);
 *   while (posix_ioctl(fd, KV_ITER_NEXT, &entry) == POSIX_OK) { ... }
 * ================================================================ */
typedef struct
{
    const char  *key;
    void        *buf;
    size_t       buf_len;
    size_t       got;
} posix_fdb_kv_io_t;

typedef struct
{
    const char  *key;
    char        *buf;
    size_t       buf_len;
    size_t       got;
} posix_fdb_kv_str_t;

typedef struct
{
    const char  *key;
    bool         exists;
} posix_fdb_kv_exists_t;

#ifndef POSIX_FDB_KV_NAME_MAX
#define POSIX_FDB_KV_NAME_MAX  64
#endif

typedef struct
{
    char         name[POSIX_FDB_KV_NAME_MAX];
    size_t       value_len;
} posix_fdb_kv_entry_t;

#define POSIX_FDB_KV_IOCTL_SET_BLOB     POSIX_IOC(POSIX_DEVICE_MAGIC_FDB, POSIX_FDB_NR(0x1, 0x1))
#define POSIX_FDB_KV_IOCTL_GET_BLOB     POSIX_IOC(POSIX_DEVICE_MAGIC_FDB, POSIX_FDB_NR(0x1, 0x2))
#define POSIX_FDB_KV_IOCTL_SET_STR      POSIX_IOC(POSIX_DEVICE_MAGIC_FDB, POSIX_FDB_NR(0x1, 0x3))
#define POSIX_FDB_KV_IOCTL_GET_STR      POSIX_IOC(POSIX_DEVICE_MAGIC_FDB, POSIX_FDB_NR(0x1, 0x4))
#define POSIX_FDB_KV_IOCTL_DEL          POSIX_IOC(POSIX_DEVICE_MAGIC_FDB, POSIX_FDB_NR(0x1, 0x5))
#define POSIX_FDB_KV_IOCTL_EXISTS       POSIX_IOC(POSIX_DEVICE_MAGIC_FDB, POSIX_FDB_NR(0x1, 0x6))
#define POSIX_FDB_KV_IOCTL_ITER_INIT    POSIX_IOC(POSIX_DEVICE_MAGIC_FDB, POSIX_FDB_NR(0x1, 0x7))
#define POSIX_FDB_KV_IOCTL_ITER_NEXT    POSIX_IOC(POSIX_DEVICE_MAGIC_FDB, POSIX_FDB_NR(0x1, 0x8))
#define POSIX_FDB_KV_IOCTL_RESET        POSIX_IOC(POSIX_DEVICE_MAGIC_FDB, POSIX_FDB_NR(0x1, 0x9))

/* ================================================================
 * TSDB (subtype = 0x2)
 *
 * 时间戳：
 *   - posix_fdb_ts_append_t.ts == 0 表示用 fdb_tsdb_init 注册的 get_time 回调
 *   - 非 0 时直接采用 caller 提供的时间戳（覆盖 get_time）
 *
 * 游标实现 (TS 原生只有回调 iter)：
 *   ITER_INIT 时驱动会一次性把命中区间的 TSL addr 缓存到 file_priv 的
 *   静态数组里（上限 CONFIG_POSIX_FDB_TS_ITER_CACHE_MAX，默认 32）。
 *   ITER_NEXT 按缓存数组顺序逐条读出。超出缓存上限返回 POSIX_ERR_NOMEM。
 *
 *   posix_fdb_ts_iter_init_t cfg = { .from = t0, .to = t1, .reverse = false };
 *   posix_ioctl(fd, TS_ITER_INIT, &cfg);
 *   posix_fdb_ts_entry_t e = { .buf = local, .buf_len = sizeof(local) };
 *   while (posix_ioctl(fd, TS_ITER_NEXT, &e) == POSIX_OK) { ... }
 * ================================================================ */
typedef int64_t posix_fdb_time_t;

#define POSIX_FDB_TS_AUTO_TIME   ((posix_fdb_time_t)0)

typedef struct
{
    const void       *buf;
    size_t            len;
    posix_fdb_time_t  ts;
} posix_fdb_ts_append_t;

typedef struct
{
    posix_fdb_time_t  from;
    posix_fdb_time_t  to;
    bool              by_time;
    bool              reverse;
} posix_fdb_ts_iter_init_t;

typedef struct
{
    void             *buf;
    size_t            buf_len;
    size_t            got;
    posix_fdb_time_t  ts;
    uint8_t           status;
    uint32_t          addr;
} posix_fdb_ts_entry_t;

typedef struct
{
    posix_fdb_time_t  from;
    posix_fdb_time_t  to;
    uint8_t           status;
    size_t            count;
} posix_fdb_ts_count_t;

typedef struct
{
    uint32_t          addr;
    uint8_t           new_status;
} posix_fdb_ts_set_status_t;

#define POSIX_FDB_TS_IOCTL_APPEND        POSIX_IOC(POSIX_DEVICE_MAGIC_FDB, POSIX_FDB_NR(0x2, 0x1))
#define POSIX_FDB_TS_IOCTL_ITER_INIT     POSIX_IOC(POSIX_DEVICE_MAGIC_FDB, POSIX_FDB_NR(0x2, 0x2))
#define POSIX_FDB_TS_IOCTL_ITER_NEXT     POSIX_IOC(POSIX_DEVICE_MAGIC_FDB, POSIX_FDB_NR(0x2, 0x3))
#define POSIX_FDB_TS_IOCTL_QUERY_COUNT   POSIX_IOC(POSIX_DEVICE_MAGIC_FDB, POSIX_FDB_NR(0x2, 0x4))
#define POSIX_FDB_TS_IOCTL_SET_STATUS    POSIX_IOC(POSIX_DEVICE_MAGIC_FDB, POSIX_FDB_NR(0x2, 0x5))
#define POSIX_FDB_TS_IOCTL_CLEAN         POSIX_IOC(POSIX_DEVICE_MAGIC_FDB, POSIX_FDB_NR(0x2, 0x6))
#define POSIX_FDB_TS_IOCTL_GET_LAST_TIME POSIX_IOC(POSIX_DEVICE_MAGIC_FDB, POSIX_FDB_NR(0x2, 0x7))

/* ================================================================
 * BF (subtype = 0x3) — Big File
 *
 * 写流程：
 *   posix_ioctl(fd, BF_CREATE, &create_arg);
 *   posix_write(fd, buf1, n1);
 *   posix_write(fd, buf2, n2);
 *   posix_ioctl(fd, BF_COMMIT, NULL);    // 或 &crc 带 CRC
 *
 * 读流程：
 *   posix_ioctl(fd, BF_SELECT_READ, "demo/hello");
 *   posix_read(fd, buf, sizeof(buf));    // 顺序读，pos 自动递增
 *
 * XIP：
 *   posix_ioctl(fd, BF_GET_XIP, &xip);   // 拿到绝对 flash 地址，可 DMA 直读
 *
 * 一个 fd 同一时刻只能"写 active"或"读 active"二选一。
 * CREATE 后必须 COMMIT 或 ABORT，否则关闭 fd 时驱动自动 ABORT。
 * ================================================================ */
#ifndef POSIX_FDB_BF_KEY_MAX
#define POSIX_FDB_BF_KEY_MAX  32
#endif

typedef struct
{
    const char  *key;
    size_t       max_size;
} posix_fdb_bf_create_t;

typedef struct
{
    uint32_t  offset;
    uint32_t  capacity;
    uint32_t  size;
    uint32_t  data_crc;
    uint32_t  flags;
} posix_fdb_bf_dirent_t;

typedef struct
{
    const char            *key;
    posix_fdb_bf_dirent_t  ent;
} posix_fdb_bf_stat_t;

typedef struct
{
    const char  *key;
    bool         exists;
} posix_fdb_bf_exists_t;

typedef struct
{
    const char  *key;
    uint32_t     xip_addr;
    size_t       size;
} posix_fdb_bf_xip_t;

typedef struct
{
    char                   key[POSIX_FDB_BF_KEY_MAX];
    posix_fdb_bf_dirent_t  ent;
    uint32_t               xip_addr;
} posix_fdb_bf_entry_t;

#define POSIX_FDB_BF_IOCTL_CREATE         POSIX_IOC(POSIX_DEVICE_MAGIC_FDB, POSIX_FDB_NR(0x3, 0x1))
#define POSIX_FDB_BF_IOCTL_COMMIT         POSIX_IOC(POSIX_DEVICE_MAGIC_FDB, POSIX_FDB_NR(0x3, 0x2))
#define POSIX_FDB_BF_IOCTL_ABORT          POSIX_IOC(POSIX_DEVICE_MAGIC_FDB, POSIX_FDB_NR(0x3, 0x3))
#define POSIX_FDB_BF_IOCTL_DELETE         POSIX_IOC(POSIX_DEVICE_MAGIC_FDB, POSIX_FDB_NR(0x3, 0x4))
#define POSIX_FDB_BF_IOCTL_DELETE_BY_ADDR POSIX_IOC(POSIX_DEVICE_MAGIC_FDB, POSIX_FDB_NR(0x3, 0x5))
#define POSIX_FDB_BF_IOCTL_STAT           POSIX_IOC(POSIX_DEVICE_MAGIC_FDB, POSIX_FDB_NR(0x3, 0x6))
#define POSIX_FDB_BF_IOCTL_EXISTS         POSIX_IOC(POSIX_DEVICE_MAGIC_FDB, POSIX_FDB_NR(0x3, 0x7))
#define POSIX_FDB_BF_IOCTL_GET_XIP        POSIX_IOC(POSIX_DEVICE_MAGIC_FDB, POSIX_FDB_NR(0x3, 0x8))
#define POSIX_FDB_BF_IOCTL_SELECT_READ    POSIX_IOC(POSIX_DEVICE_MAGIC_FDB, POSIX_FDB_NR(0x3, 0x9))
#define POSIX_FDB_BF_IOCTL_FOREACH_INIT   POSIX_IOC(POSIX_DEVICE_MAGIC_FDB, POSIX_FDB_NR(0x3, 0xA))
#define POSIX_FDB_BF_IOCTL_FOREACH_NEXT   POSIX_IOC(POSIX_DEVICE_MAGIC_FDB, POSIX_FDB_NR(0x3, 0xB))

#ifdef __cplusplus
}
#endif

#endif /* POSIX_IOCTL_FDB_H */
