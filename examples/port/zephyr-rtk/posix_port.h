/* RTL8773G + Zephyr RTOS posix-device port */

#ifndef POSIX_PORT_RTK_ZEPHYR_H
#define POSIX_PORT_RTK_ZEPHYR_H

#include "posix.h"
#include "posix_init.h"

/*
 * 本 port 的同步模型：
 *   框架只需要 (1) 一个全局互斥锁保护设备表/fd 池等元数据，
 *   (2) 一个 ISR 上下文判断。两者在 posix_port_init.c 中用
 *   RTK OS / Zephyr 原语实现：
 *     - posix_lock / posix_unlock → 单个 os_mutex（Zephyr k_mutex，递归）
 *     - posix_port_in_isr         → k_is_in_isr()
 *
 *   本 port【不】提供 semaphore / message-queue / thread / timer 等
 *   POSIX 对象，也没有这类对象的静态池；各驱动的 per-open 数据由
 *   驱动自身的静态文件池管理（见各 posix_port_*.c）。
 *
 *   posix_lock / posix_unlock / posix_port_in_isr 的原型由
 *   posix_device.h 统一声明（框架与 port 共用同一原型），此处不再重复。
 */

/* port 启动入口（框架未声明，仅本 port 提供） */
int  posix_port_init_all(void);
void posix_port_lock_init(void);

#endif /* POSIX_PORT_RTK_ZEPHYR_H */
