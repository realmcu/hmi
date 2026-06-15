/* ================================================================
 * POSIX 设备抽象层 — 核心实现
 *
 * 负责：
 * 1. 设备表管理（注册/注销/查找，支持挂载点前缀匹配）
 * 2. posix_open → 查找设备表 → 占用引用 → 调 ops->open → 存 file_priv
 * 3. posix_read/write/ioctl/close → 校验 fd → 取快照 → 锁外调 ops
 * 4. ISR 版跳过锁，仅做句柄有效性校验后直调 ops
 *
 * === 线程安全策略（重要） ===
 *   框架锁只保护“设备表 / fd 池 / 引用计数”这类框架元数据。
 *   每个 API 在锁内完成：句柄有效性校验 + 设备查找 + 取出
 *   (ops 指针, drv_data, file_priv) 快照，然后【释放锁】再调用
 *   驱动 ops 回调。因此：
 *     - 驱动 ops 可以安全阻塞/睡眠，不会串行化其它设备或卡死系统。
 *     - 一个已 open 的 fd 令对应设备 ref_count > 0，阻止其被 unregister，
 *       保证锁外执行 ops 期间设备表项与 drv_data 不会被销毁。
 *   posix_*_isr 不能加锁（互斥量在 ISR 不可用），只做有效性校验后直调 ops；
 *   调用方需保证 ISR 访问的设备不会被并发 unregister。
 *
 * 移植到新平台不需要修改此文件，
 * 只需要实现 posix_lock/posix_unlock/posix_port_in_isr（默认 weak 实现）。
 * ================================================================ */

#include "posix_device.h"
#include <stdio.h>
#include <string.h>

/* ================================================================
 * 内部数据结构
 * ================================================================ */

#ifndef POSIX_DEVICE_TABLE_SIZE
#define POSIX_DEVICE_TABLE_SIZE  32
#endif

#ifndef POSIX_FD_POOL_SIZE
#define POSIX_FD_POOL_SIZE       16   /* 最大同时打开的文件数 */
#endif

/* fd 有效性魔数 ('pxfd')，用于检测野指针/已 close 的陈旧句柄 */
#define POSIX_FD_MAGIC  0x70786664u

/* 设备表条目 */
typedef struct {
    char                     path[32];
    const posix_driver_ops_t *ops;
    void                     *drv_data;       /* 驱动私有数据 */
    int                      ref_count;
    int                      in_use;
} posix_device_entry_t;

/* fd 结构体 — open 时从池中分配 */
struct posix_device {
    uint32_t              magic;      /* == POSIX_FD_MAGIC 表示有效 */
    posix_device_entry_t *entry;      /* 指向设备表 */
    void                 *file_priv;  /* per-open 私有数据（ops->open 返回值）*/
    int                   in_use;     /* 此 fd 是否在使用中 */
};

/* ---------- 静态资源 ---------- */
static posix_device_entry_t s_dev_table[POSIX_DEVICE_TABLE_SIZE];
static struct posix_device s_fd_pool[POSIX_FD_POOL_SIZE];

/* ================================================================
 * 移植层弱符号默认实现（port 层用强符号覆盖）
 * ================================================================ */

void posix_lock(void)   __attribute__((weak));
void posix_unlock(void) __attribute__((weak));
void posix_lock(void)   { /* 默认空，用户覆盖 */ }
void posix_unlock(void) { /* 默认空，用户覆盖 */ }

int posix_port_in_isr(void) __attribute__((weak));
int posix_port_in_isr(void) { return 0; }

/* ================================================================
 * 内部辅助函数
 * ================================================================ */

/* ---- 设备表操作 ---- */
static posix_device_entry_t *entry_by_path(const char *path)
{
    for (int i = 0; i < POSIX_DEVICE_TABLE_SIZE; i++) {
        if (s_dev_table[i].in_use &&
            strcmp(s_dev_table[i].path, path) == 0) {
            return &s_dev_table[i];
        }
    }
    return NULL;
}

/* open 专用查找：先精确匹配；失败则做挂载点前缀匹配——
 * 注册路径是 query 的前缀且其后紧跟 '/'（取最长匹配）。
 * 这让注册的 "/dev/gpio0" 能服务 "/dev/gpio0/p12" 这类引脚级路径，
 * 完整 path 会传给 ops->open 由驱动解析剩余子路径。 */
static posix_device_entry_t *entry_for_open(const char *path)
{
    posix_device_entry_t *exact = entry_by_path(path);
    if (exact) { return exact; }

    posix_device_entry_t *best = NULL;
    size_t best_len = 0;
    for (int i = 0; i < POSIX_DEVICE_TABLE_SIZE; i++) {
        if (!s_dev_table[i].in_use) { continue; }
        size_t plen = strlen(s_dev_table[i].path);
        if (plen > best_len &&
            strncmp(s_dev_table[i].path, path, plen) == 0 &&
            path[plen] == '/') {
            best     = &s_dev_table[i];
            best_len = plen;
        }
    }
    return best;
}

static posix_device_entry_t *free_entry(void)
{
    for (int i = 0; i < POSIX_DEVICE_TABLE_SIZE; i++) {
        if (!s_dev_table[i].in_use) return &s_dev_table[i];
    }
    return NULL;
}

/* ---- fd 池操作 ---- */
static struct posix_device *alloc_fd(void)
{
    for (int i = 0; i < POSIX_FD_POOL_SIZE; i++) {
        if (!s_fd_pool[i].in_use) {
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

/* 校验 fd 句柄：必须落在 fd 池区间内、对齐、魔数正确、在用。
 * 用于拦截野指针、栈地址、以及 close 后被复用的陈旧句柄。
 * 调用者需自行决定是否在持锁状态下调用。 */
static int fd_is_valid(const struct posix_device *f)
{
    if (f < &s_fd_pool[0] || f >= &s_fd_pool[POSIX_FD_POOL_SIZE]) {
        return 0;
    }
    if ((size_t)((const char *)f - (const char *)&s_fd_pool[0])
            % sizeof(s_fd_pool[0]) != 0) {
        return 0;
    }
    if (f->magic != POSIX_FD_MAGIC || !f->in_use) {
        return 0;
    }
    return 1;
}

/* ================================================================
 * 设备注册 API
 * ================================================================ */

int posix_device_register(const char *path,
                          const posix_driver_ops_t *ops,
                          void *drv_data)
{
    if (!path || !ops) return POSIX_ERR_INVAL;

    posix_lock();

    if (entry_by_path(path)) {
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
    int done = 0;   /* 已成功注册的项数，回滚时只撤销这些 */

    for (int i = 0; i < count; i++) {
        char path[32];
        int n = snprintf(path, sizeof(path), fmt, i);
        if (n < 0 || (size_t)n >= sizeof(path)) goto rollback;
        int ret = posix_device_register(path, ops,
                                        privs ? privs[i] : NULL);
        if (ret != POSIX_OK) goto rollback;
        done++;
    }
    return POSIX_OK;

rollback:
    for (int i = 0; i < done; i++) {
        char path[32];
        snprintf(path, sizeof(path), fmt, i);
        posix_device_unregister(path);
    }
    return POSIX_ERR_NOMEM;
}

int posix_device_unregister(const char *path)
{
    if (!path) return POSIX_ERR_INVAL;

    posix_lock();
    posix_device_entry_t *e = entry_by_path(path);
    if (!e)  { posix_unlock(); return POSIX_ERR_NODEV; }
    if (e->ref_count > 0) { posix_unlock(); return POSIX_ERR_BUSY; }
    memset(e, 0, sizeof(*e));
    posix_unlock();
    return POSIX_OK;
}

/* ================================================================
 * POSIX 用户 API
 *
 * 统一模式：锁内校验 + 取快照，锁外调 ops。
 * ================================================================ */

posix_fd_t posix_open(const char *path)
{
    if (!path) return POSIX_FD_NULL;

    posix_lock();

    posix_device_entry_t *e = entry_for_open(path);
    if (!e) { posix_unlock(); return POSIX_FD_NULL; }

    struct posix_device *fd = alloc_fd();
    if (!fd) { posix_unlock(); return POSIX_FD_NULL; }

    fd->entry = e;
    e->ref_count++;     /* 先占引用，保证锁外执行 open 期间设备不被注销 */

    const posix_driver_ops_t *ops = e->ops;
    void                     *drv = e->drv_data;
    posix_unlock();

    /* 锁外调用驱动 open（可阻塞） */
    void *fp;
    if (ops && ops->open) {
        fp = ops->open(drv, path);
    } else {
        fp = drv;   /* 没 open 回调时复用 drv_data */
    }

    if (fp == POSIX_OPEN_ERR) {
        /* 驱动 open 失败：回滚引用与 fd */
        posix_lock();
        if (e->ref_count > 0) e->ref_count--;
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
    if (!fd) return POSIX_ERR_INVAL;
    struct posix_device *f = (struct posix_device *)fd;

    posix_lock();
    if (!fd_is_valid(f)) { posix_unlock(); return POSIX_ERR_INVAL; }

    posix_device_entry_t     *e   = f->entry;
    const posix_driver_ops_t *ops = e ? e->ops : NULL;
    void                     *drv = e ? e->drv_data : NULL;
    void                     *fp  = f->file_priv;

    /* 立即让 fd 失效，杜绝并发/重复 close 命中同一句柄 */
    free_fd(f);
    if (e && e->ref_count > 0) e->ref_count--;
    posix_unlock();

    /* 锁外调用驱动 close（可阻塞）；ops 指向静态表、drv/fp 为快照，均安全 */
    int ret = POSIX_OK;
    if (ops && ops->close) {
        ret = ops->close(drv, fp);
    }
    return ret;
}

posix_ssize_t posix_read(posix_fd_t fd, void *buf, size_t count)
{
    if (!fd || !buf || !count) return POSIX_ERR_INVAL;
    struct posix_device *f = (struct posix_device *)fd;

    posix_lock();
    if (!fd_is_valid(f)) { posix_unlock(); return POSIX_ERR_INVAL; }
    posix_device_entry_t *e = f->entry;
    if (!e || !e->in_use || !e->ops || !e->ops->read) {
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
    if (!fd || !buf || !count) return POSIX_ERR_INVAL;
    struct posix_device *f = (struct posix_device *)fd;

    posix_lock();
    if (!fd_is_valid(f)) { posix_unlock(); return POSIX_ERR_INVAL; }
    posix_device_entry_t *e = f->entry;
    if (!e || !e->in_use || !e->ops || !e->ops->write) {
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
    if (!fd) return POSIX_ERR_INVAL;
    struct posix_device *f = (struct posix_device *)fd;

    posix_lock();
    if (!fd_is_valid(f)) { posix_unlock(); return POSIX_ERR_INVAL; }
    posix_device_entry_t *e = f->entry;
    if (!e || !e->in_use || !e->ops || !e->ops->ioctl) {
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
 * ISR 安全版本（跳过锁，直接调驱动 ops）
 *
 * 不加锁（RTOS 互斥量在 ISR 上下文不可用），只做句柄有效性校验。
 * 不再修改 cmd —— 驱动统一通过 posix_port_in_isr() 判断上下文，
 * 避免污染 cmd 命名空间。
 * 调用方需保证 ISR 访问的设备不会被并发 unregister。
 * ================================================================ */

posix_ssize_t posix_read_isr(posix_fd_t fd, void *buf, size_t count)
{
    if (!fd || !buf || !count) return POSIX_ERR_INVAL;
    struct posix_device *f = (struct posix_device *)fd;
    if (!fd_is_valid(f)) return POSIX_ERR_INVAL;
    posix_device_entry_t *e = f->entry;
    if (!e || !e->in_use || !e->ops || !e->ops->read) return POSIX_ERR_NODEV;
    return e->ops->read(e->drv_data, f->file_priv, buf, count);
}

posix_ssize_t posix_write_isr(posix_fd_t fd, const void *buf, size_t count)
{
    if (!fd || !buf || !count) return POSIX_ERR_INVAL;
    struct posix_device *f = (struct posix_device *)fd;
    if (!fd_is_valid(f)) return POSIX_ERR_INVAL;
    posix_device_entry_t *e = f->entry;
    if (!e || !e->in_use || !e->ops || !e->ops->write) return POSIX_ERR_NODEV;
    return e->ops->write(e->drv_data, f->file_priv, buf, count);
}

int posix_ioctl_isr(posix_fd_t fd, unsigned long cmd, void *arg)
{
    if (!fd) return POSIX_ERR_INVAL;
    struct posix_device *f = (struct posix_device *)fd;
    if (!fd_is_valid(f)) return POSIX_ERR_INVAL;
    posix_device_entry_t *e = f->entry;
    if (!e || !e->in_use || !e->ops || !e->ops->ioctl) return POSIX_ERR_NODEV;
    return e->ops->ioctl(e->drv_data, f->file_priv, cmd, arg);
}
