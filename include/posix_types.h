#ifndef POSIX_TYPES_H
#define POSIX_TYPES_H

#include <stdint.h>
#include <stddef.h>

/* ================================================================
 * 基础类型定义
 *
 * 设计原则：
 * - 对外只暴露不透明指针 (posix_fd_t)，隐藏实现细节
 * - 不依赖任何 libc 函数（只用 stdint/stddef 的类型定义）
 * - 错误码用负值，沿用 Linux 内核内部风格（直接返回 -errno），
 *   注意：这与 Linux 用户态 POSIX(返回 -1 并设 errno) 不同，
 *   本层不提供全局 errno，错误信息全部承载在返回值里。
 * ================================================================ */

/* ---------- 设备句柄 ---------- */
typedef struct posix_device *posix_fd_t;

#define POSIX_FD_NULL  ((posix_fd_t)0)

/* ---------- 有符号大小类型 ----------
 * read/write 的返回值类型：>=0 表示传输字节数，<0 表示错误码。
 * 用 ptrdiff_t 保证与 size_t 等宽的有符号范围（即 SSIZE_MAX 量级），
 * 避免把 size_t 的字节数截断进 int 而与负错误码混淆。
 */
typedef ptrdiff_t posix_ssize_t;

/* ---------- 统一错误码（负值） ---------- */
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
#define POSIX_ERR_ISR      -10   /* 禁止在 ISR 中调用此 API */

/* ---------- ops->open 失败哨兵 ----------
 * open 回调返回 file_priv（可为 NULL，表示该 fd 无 per-open 私有数据）。
 * 若驱动 open 失败（如资源耗尽），必须返回 POSIX_OPEN_ERR，
 * 框架据此回滚 fd 分配并让 posix_open 返回 POSIX_FD_NULL。
 * 这样 NULL（合法的空私有数据）与失败可以明确区分。
 */
#define POSIX_OPEN_ERR      ((void *)-1)

/* ---------- ISR 上下文标记（已废弃） ----------
 * 历史上 posix_ioctl_isr() 会把此位 OR 进 cmd 来传递 ISR 上下文，
 * 这会污染 cmd 命名空间、要求每个驱动手动屏蔽，极易出错。
 * 现已改为：所有上下文判断统一通过 posix_port_in_isr()，
 * 框架不再修改 cmd。保留此宏仅为向后兼容，新代码请勿使用。
 */
#define POSIX_FLAG_ISR      (1UL << 31)

#endif /* POSIX_TYPES_H */
