#ifndef POSIX_DEVICE_H
#define POSIX_DEVICE_H

#include "posix_types.h"
#include "posix_ioctl.h"

/* ================================================================
 * Core driver interface + device registration/lookup
 *
 * This is the core of the whole abstraction layer, made of three parts:
 *
 * 1. posix_driver_ops_t - driver vtable (5 methods)
 *    the open callback returns this fd's private data, stored in fd->file_priv
 *    later read/write/ioctl/close access it through file_priv
 *
 * 2. posix_device_register() - register a driver with the framework
 *
 * 3. posix_open/close/read/write/ioctl - the API users see
 *    5 functions cover all peripherals
 *
 * === Thread-safety strategy ===
 *   - posix_open/close/read/write/ioctl complete inside the framework lock:
 *     "handle validation + device lookup + snapshot of ops/file_priv",
 *     then [release the lock] and invoke the driver ops callbacks.
 *     So driver ops may safely block/sleep and will not serialize other
 *     devices or stall the whole system by holding a global lock.
 *   - An open fd makes the device's ref_count > 0,
 *     which prevents the device from being unregistered and guarantees
 *     the device table entry is not destroyed while ops run outside the lock.
 *   - The posix_*_isr variants skip the framework lock (mutexes are unusable in ISRs),
 *     are for interrupt context only, and call ops directly after a handle validity check.
 *   - The default lock is a no-op (single-threaded/bare-metal);
 *     multi-threaded RTOSes must implement posix_lock/unlock.
 *
 * Adding a new device type = 4 steps:
 *   1. Define the magic (bottom section)
 *   2. Write the ioctl cmd header
 *   3. Implement the 5 posix_driver_ops_t functions
 *   4. Call posix_device_register() to register
 * ================================================================ */

/* ---------- Device type magic (unique per device type) ---------- */
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
#define POSIX_DEVICE_MAGIC_RTC     0x0C
#define POSIX_DEVICE_MAGIC_MOTOR   0x0D
#define POSIX_DEVICE_MAGIC_HRM     0x0E
/* 0x0D~0xEF reserved for future device types */
/* 0xF0~0xFF reserved for user-defined devices */

/* ---------- Driver vtable ---------- */
/*
 * open callback:
 *   in drv_data = the priv passed to posix_device_register
 *   in path     = full path from posix_open (supports mount-point subpaths,
 *                 e.g. registering "/dev/gpio0" then open "/dev/gpio0/p12"
 *                 passes the full path in, and the driver parses the remainder)
 *   return file_priv is saved in the fd and passed to all later callbacks.
 *   - with no per-open state it may return drv_data itself, or return NULL
 *   - on failure it must return POSIX_OPEN_ERR (see posix_types.h),
 *     and the framework rolls back and makes posix_open return POSIX_FD_NULL
 *
 * close/read/write/ioctl:
 *   in drv_data = the priv passed to posix_device_register
 *   in file_priv = the return value of the open callback
 *
 * read/write return posix_ssize_t: >=0 is bytes transferred, <0 is an error code.
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

/* ---------- Driver registration API ---------- */
int posix_device_register(const char *path,
                          const posix_driver_ops_t *ops,
                          void *priv);

int posix_device_register_group(const char *fmt, int count,
                                const posix_driver_ops_t *ops,
                                void **privs);

int posix_device_unregister(const char *path);

/* Look up ops / drv_data of a registered device; used to register an "alias" by path.
 * Returns POSIX_OK and fills *ops_out / *drv_out; returns POSIX_ERR_NODEV if not found.
 * Note: a subsequent posix_device_register(alias, ops, drv) creates an alias entry
 * sharing ops+drv_data with the source path. ref_count is tracked per entry,
 * so unregistering after opening both alias and source requires closing each first.
 */
int posix_device_lookup(const char *path,
                        const posix_driver_ops_t **ops_out,
                        void **drv_out);

/* Alias registration: mount an already-registered src_path again under the alias name;
 * both paths share the same ops+drv_data, and opening either enters the same device.
 * This is the generic mechanism for "multiple peripheral models + current board selection" (touch/gsensor/...):
 *   posix_device_bind_alias("/dev/touch0",   "/dev/cst816d");
 *   posix_device_bind_alias("/dev/gsensor0", "/dev/sc7a20");
 * Return values:
 *   POSIX_OK        success
 *   POSIX_ERR_NODEV src_path not registered
 *   POSIX_ERR_BUSY  alias already exists (unbind first to override)
 *   POSIX_ERR_INVAL null argument or alias too long
 */
int posix_device_bind_alias(const char *alias, const char *src_path);

/* Unbind: equivalent to posix_device_unregister(alias), only for the alias entry.
 * Returns POSIX_ERR_BUSY if the alias still has an unclosed fd.
 */
int posix_device_unbind_alias(const char *alias);

/* ---------- POSIX user API ---------- */
posix_fd_t posix_open(const char *path);
int        posix_close(posix_fd_t fd);

posix_ssize_t posix_read(posix_fd_t fd, void *buf, size_t count);
posix_ssize_t posix_write(posix_fd_t fd, const void *buf, size_t count);
int           posix_ioctl(posix_fd_t fd, unsigned long cmd, void *arg);

/* ISR-safe variants (skip the framework lock, call driver ops directly) */
posix_ssize_t posix_read_isr(posix_fd_t fd, void *buf, size_t count);
posix_ssize_t posix_write_isr(posix_fd_t fd, const void *buf, size_t count);
int           posix_ioctl_isr(posix_fd_t fd, unsigned long cmd, void *arg);

/* ---------- Porting layer interface (implemented by port/, shared prototype) ----------
 * On single-threaded/bare-metal, posix_lock/unlock may be left empty;
 * multi-threaded RTOSes must implement a mutex. posix_port_in_isr returns non-0 for interrupt context.
 * The framework provides weak default implementations (empty lock / always 0); port overrides with strong symbols.
 */
void posix_lock(void);
void posix_unlock(void);
int  posix_port_in_isr(void);

/* Semaphore --- for interrupt-driven drivers (ISR-safe give)
 * timeout_ms: milliseconds to wait, 0xFFFFFFFF means block forever, 0 means non-blocking
 * return: 0 success, -1 failure/timeout
 */
void *posix_sem_create(const char *name, uint32_t init_count, uint32_t max_count);
void  posix_sem_delete(void *sem);
int   posix_sem_give(void *sem);
int   posix_sem_take(void *sem, uint32_t timeout_ms);

#endif /* POSIX_DEVICE_H */
