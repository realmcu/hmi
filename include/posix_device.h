#ifndef POSIX_DEVICE_H
#define POSIX_DEVICE_H

#include "posix_types.h"
#include "posix_ioctl.h"

/* ================================================================
 * 核心驱动接口 + 设备注册/查找
 *
 * 这是整个抽象层的核心，由三部分组成：
 *
 * 1. posix_driver_ops_t — 驱动虚函数表（5 个方法）
 *    open 回调返回此 fd 的私有数据，存入 fd->file_priv
 *    后续 read/write/ioctl/close 通过 file_priv 访问
 *
 * 2. posix_device_register() — 驱动注册到框架
 *
 * 3. posix_open/close/read/write/ioctl — 用户看到的 API
 *    5 个函数通吃所有外设
 *
 * === 线程安全策略 ===
 *   - posix_open/close/read/write/ioctl 在框架锁内完成
 *     “句柄校验 + 设备查找 + 取 ops/file_priv 快照”，
 *     随后【释放锁】再执行驱动 ops 回调。
 *     因此驱动 ops 可以安全阻塞/睡眠，不会因持有全局锁而
 *     串行化其它设备或导致全系统假死。
 *   - 一个已 open 的 fd 会令对应设备的 ref_count > 0，
 *     从而阻止该设备被 unregister，保证锁外执行 ops 期间
 *     设备表项不会被销毁。
 *   - posix_*_isr 版本跳过框架锁（互斥量在 ISR 不可用），
 *     专用于中断上下文；只做句柄有效性校验后直调 ops。
 *   - 默认锁为空操作（单线程/裸机），
 *     多线程 RTOS 需实现 posix_lock/unlock。
 *
 * 添加新设备类型 = 4 步：
 *   1. 定义 magic（底部区域）
 *   2. 写 ioctl cmd 头文件
 *   3. 实现 posix_driver_ops_t 的 5 个函数
 *   4. 调 posix_device_register() 注册
 * ================================================================ */

/* ---------- 设备类型 magic（每个设备类型唯一） ---------- */
#define POSIX_DEVICE_MAGIC_UART    0x01
#define POSIX_DEVICE_MAGIC_SPI     0x02
#define POSIX_DEVICE_MAGIC_GPIO    0x03
#define POSIX_DEVICE_MAGIC_PWM     0x04
#define POSIX_DEVICE_MAGIC_ADC     0x05
#define POSIX_DEVICE_MAGIC_SDIO    0x06
#define POSIX_DEVICE_MAGIC_LCD     0x07
#define POSIX_DEVICE_MAGIC_TOUCH   0x08
#define POSIX_DEVICE_MAGIC_GSENSOR 0x09
#define POSIX_DEVICE_MAGIC_FDB     0x0A   /* FlashDB (KV/TS/BF, path-multiplexed) */
#define POSIX_DEVICE_MAGIC_I2C     0x0B
/* 0x0C~0xEF 保留给将来设备类型 */
/* 0xF0~0xFF 保留给用户自定义设备 */

/* ---------- 驱动虚函数表 ---------- */
/*
 * open 回调：
 *   入参 drv_data = posix_device_register 传入的 priv
 *   入参 path     = posix_open 的完整路径（支持挂载点子路径，
 *                   例如注册 "/dev/gpio0" 时 open "/dev/gpio0/p12"
 *                   会把完整 path 传进来，由驱动解析剩余部分）
 *   返回值 file_priv 会保存在 fd 中，后续所有回调都能拿到。
 *   - 无 per-open 状态时可返回 drv_data 本身，或返回 NULL
 *   - 失败时必须返回 POSIX_OPEN_ERR（见 posix_types.h），
 *     框架会据此回滚并让 posix_open 返回 POSIX_FD_NULL
 *
 * close/read/write/ioctl：
 *   入参 drv_data = posix_device_register 传入的 priv
 *   入参 file_priv = open 回调的返回值
 *
 * read/write 返回 posix_ssize_t：>=0 为传输字节数，<0 为错误码。
 */
typedef struct posix_driver_ops
{
    void         *(*open)(void *drv_data, const char *path);
    int (*close)(void *drv_data, void *file_priv);
    posix_ssize_t (*read)(void *drv_data, void *file_priv,
                          void *buf, size_t count);
    posix_ssize_t (*write)(void *drv_data, void *file_priv,
                           const void *buf, size_t count);
    int (*ioctl)(void *drv_data, void *file_priv,
                 unsigned long cmd, void *arg);
} posix_driver_ops_t;

/* ---------- 驱动注册 API ---------- */
int posix_device_register(const char *path,
                          const posix_driver_ops_t *ops,
                          void *priv);

int posix_device_register_group(const char *fmt, int count,
                                const posix_driver_ops_t *ops,
                                void **privs);

int posix_device_unregister(const char *path);

/* ---------- POSIX 用户 API ---------- */
posix_fd_t posix_open(const char *path);
int        posix_close(posix_fd_t fd);

posix_ssize_t posix_read(posix_fd_t fd, void *buf, size_t count);
posix_ssize_t posix_write(posix_fd_t fd, const void *buf, size_t count);
int           posix_ioctl(posix_fd_t fd, unsigned long cmd, void *arg);

/* ISR 安全版（跳过框架锁，直接调驱动 ops） */
posix_ssize_t posix_read_isr(posix_fd_t fd, void *buf, size_t count);
posix_ssize_t posix_write_isr(posix_fd_t fd, const void *buf, size_t count);
int           posix_ioctl_isr(posix_fd_t fd, unsigned long cmd, void *arg);

/* ---------- 移植层接口（由 port/ 实现，框架与 port 共用此原型） ----------
 * 单线程/裸机下 posix_lock/unlock 可留空；
 * 多线程 RTOS 需实现互斥锁。posix_port_in_isr 返回非 0 表示中断上下文。
 * 框架提供 weak 默认实现（空锁 / 恒返回 0），port 强符号覆盖。
 */
void posix_lock(void);
void posix_unlock(void);
int  posix_port_in_isr(void);

/* 信号量 --- 供中断驱动型驱动使用（ISR-safe give）
 * timeout_ms: 等待毫秒数，0xFFFFFFFF 表示永久阻塞，0 表示非阻塞
 * 返回值: 0 成功，-1 失败/超时
 */
void *posix_sem_create(const char *name, uint32_t init_count, uint32_t max_count);
void  posix_sem_delete(void *sem);
int   posix_sem_give(void *sem);
int   posix_sem_take(void *sem, uint32_t timeout_ms);

#endif /* POSIX_DEVICE_H */
