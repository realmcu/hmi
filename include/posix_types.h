#ifndef POSIX_TYPES_H
#define POSIX_TYPES_H

#include <stdint.h>
#include <stddef.h>

/* ================================================================
 * Basic type definitions
 *
 * Design principles:
 * - Expose only an opaque pointer (posix_fd_t), hiding implementation details
 * - Depend on no libc functions (only stdint/stddef type definitions)
 * - Use negative error codes, following the Linux kernel internal style (return -errno directly),
 *   Note: this differs from userspace POSIX (return -1 and set errno);
 *   this layer provides no global errno, all error info is carried in the return value.
 * ================================================================ */

/* ---------- Device handle ---------- */
typedef struct posix_device *posix_fd_t;

#define POSIX_FD_NULL  ((posix_fd_t)0)

/* ---------- Signed size type ----------
 * Return type of read/write: >=0 means bytes transferred, <0 means error code.
 * ptrdiff_t gives a signed range as wide as size_t (i.e. SSIZE_MAX scale),
 * avoiding truncating size_t byte counts into int and confusing them with negative errors.
 */
typedef ptrdiff_t posix_ssize_t;

/* ---------- Unified error codes (negative) ---------- */
#define POSIX_OK            0
#define POSIX_ERR          -1
#define POSIX_ERR_IO       -2
#define POSIX_ERR_TIMEOUT  -3
#define POSIX_ERR_BUSY     -4
#define POSIX_ERR_INVAL    -5
#define POSIX_ERR_NODEV    -6
#define POSIX_ERR_NOMEM    -7
#define POSIX_ERR_AGAIN    -8
#define POSIX_ERR_NOSUPP   -9
#define POSIX_ERR_ISR      -10   /* This API must not be called from an ISR */

/* ---------- ops->open failure sentinel ----------
 * The open callback returns file_priv (may be NULL, meaning this fd has no per-open private data).
 * If driver open fails (e.g. out of resources), it must return POSIX_OPEN_ERR;
 * the framework then rolls back the fd allocation and makes posix_open return POSIX_FD_NULL.
 * This lets NULL (valid empty private data) be clearly distinguished from failure.
 */
#define POSIX_OPEN_ERR      ((void *)-1)

/* ---------- ISR context flag (deprecated) ----------
 * Historically posix_ioctl_isr() ORed this bit into cmd to pass ISR context,
 * which polluted the cmd namespace, required every driver to mask it manually, and was error-prone.
 * Now all context checks go through posix_port_in_isr() and the framework no longer
 * modifies cmd. This macro is kept only for backward compatibility; do not use it in new code.
 */
#define POSIX_FLAG_ISR      (1UL << 31)

#endif /* POSIX_TYPES_H */
