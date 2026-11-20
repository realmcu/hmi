#ifndef POSIX_PORT_H
#define POSIX_PORT_H

/* ================================================================
 * Platform porting interface
 *
 * Functions to implement when porting to a new RTOS:
 *   1. posix_lock() / posix_unlock()  — multi-thread protection
 *   2. posix_port_in_isr()            — ISR context detection
 *
 * Each driver auto-registers via POSIX_INIT_DEVICE_EXPORT(fn)
 * No need to explicitly call a registration function in the port layer
 * ================================================================ */

#include "posix.h"
#include "posix_init.h"

/* One-time init: locks + auto-register all devices */
int posix_port_init_all(void);

/* Internal lock init */
void posix_port_lock_init(void);

/* Millisecond delay (callable from task context, not from an ISR) */
void posix_port_delay_ms(uint32_t ms);

/* Free-running cycle counter for short relative timing measurements. */
uint32_t posix_port_cycle_get_32(void);

/* ================================================================
 * Static memory pool pattern (recommended for heap-less embedded environments)
 *
 * Each driver must allocate a file_priv struct for open.
 * In a heap-less environment, prefer a static pool instead of malloc:
 *
 *   #define MAX_FILES  4
 *   static my_file_t s_pool[MAX_FILES];
 *   static int s_used[MAX_FILES];
 *
 *   static void *my_open(void *drv_data, const char *path) {
 *       for (int i = 0; i < MAX_FILES; i++) {
 *           if (!s_used[i]) { s_used[i] = 1; return &s_pool[i]; }
 *       }
 *       return NULL;  // pool full
 *   }
 *
 *   static int my_close(void *drv_data, void *file_priv) {
 *       my_file_t *f = (my_file_t *)file_priv;
 *       int idx = f - s_pool;  // pointer subtraction yields the index
 *       if (idx >= 0 && idx < MAX_FILES) s_used[idx] = 0;
 *       return POSIX_OK;
 *   }
 *
 * For each driver's pool size, see the max concurrently-open count in PORTING.md.
 * ================================================================ */

#endif /* POSIX_PORT_H */
