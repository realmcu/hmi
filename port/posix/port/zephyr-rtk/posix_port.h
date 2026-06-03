/* RTL8773G + Zephyr RTOS posix-device port */

#ifndef POSIX_PORT_RTK_ZEPHYR_H
#define POSIX_PORT_RTK_ZEPHYR_H

#include <stdbool.h>
#include "posix.h"
#include "posix_init.h"

/*
 * Static pool usage:
 * All POSIX objects (mutexes, semaphores, message queues, threads, timers)
 * are allocated from fixed-size static pools at compile time.  The pool
 * sizes are controlled by the CONFIG_POSIX_* Kconfig symbols and must be
 * large enough to satisfy the maximum concurrent object counts required by
 * the application.  No heap allocation is performed at runtime.
 */

int  posix_port_init_all(void);
void posix_port_lock_init(void);
bool posix_port_in_isr(void);

#endif /* POSIX_PORT_RTK_ZEPHYR_H */
