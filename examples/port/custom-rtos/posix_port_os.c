/* ================================================================
 * Platform init — when porting to your RTOS you only need to modify this file
 *
 * Steps (see step one in PORTING.md):
 *   1. Include your RTOS synchronization primitive headers
 *   2. Implement posix_lock / posix_unlock (global mutex protecting framework metadata)
 *   3. Implement posix_port_in_isr (ISR context check, non-0 = inside an ISR)
 *   4. Create the mutex in posix_port_lock_init
 *
 * Single-threaded/bare-metal: leave posix_lock/unlock empty and have posix_port_in_isr always return 0.
 * ================================================================ */

#include "posix_port.h"

/* --- Include your RTOS synchronization primitive headers ---
 * #include "your_rtos.h"
 */

/* --- Mutex handle (created by posix_port_lock_init) ---
 * static your_mutex_t s_posix_mutex;
 */

void posix_lock(void)
{
    /* Multi-threaded RTOS:
     * your_rtos_mutex_lock(&s_posix_mutex); */
}

void posix_unlock(void)
{
    /* Multi-threaded RTOS:
     * your_rtos_mutex_unlock(&s_posix_mutex); */
}

int posix_port_in_isr(void)
{
    /* Most RTOSes have a ready-made interface:
     * return your_rtos_in_isr();
     *
     * If not, maintain a global flag on ISR entry/exit:
     * extern volatile int g_in_isr;
     * return g_in_isr; */
    return 0;
}

void posix_port_lock_init(void)
{
    /* Multi-threaded RTOS:
     * your_rtos_mutex_create(&s_posix_mutex); */
}

void *posix_sem_create(const char *name, uint32_t init_count, uint32_t max_count)
{
    /* Replace with your RTOS implementation, e.g.:
     * your_sem_t *sem = your_rtos_sem_create(init_count, max_count);
     * return sem;
     */
    (void)name; (void)init_count; (void)max_count;
    return NULL;
}

void posix_sem_delete(void *sem)
{
    /* your_rtos_sem_delete(sem); */
    (void)sem;
}

int posix_sem_give(void *sem)
{
    /* return your_rtos_sem_give(sem) ? 0 : -1; */
    (void)sem;
    return -1;
}

int posix_sem_take(void *sem, uint32_t timeout_ms)
{
    /* return your_rtos_sem_take(sem, timeout_ms) ? 0 : -1; */
    (void)sem; (void)timeout_ms;
    return -1;
}

uint32_t posix_port_cycle_get_32(void)
{
    /* Replace with the target RTOS free-running cycle counter. */
    return 0;
}

int posix_port_init_all(void)
{
    posix_port_lock_init();
    return posix_auto_init();
}
