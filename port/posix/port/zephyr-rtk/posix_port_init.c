/**
 * @file posix_port_init.c
 * @brief POSIX port initialization for RTK8773G + Zephyr
 */

#include "posix_port.h"
#include "os_sync.h"
#include <zephyr/kernel.h>

/* RTK OS mutex handle */
static void *s_posix_mutex = NULL;

/* RTK OS API declarations */
extern bool os_mutex_create(void **pp_handle);
extern bool os_mutex_take(void *p_handle, uint32_t wait_ms);
extern bool os_mutex_give(void *p_handle);

#define OS_WAIT_FOREVER 0xFFFFFFFFU

void posix_lock(void)
{
    if (s_posix_mutex)
    {
        os_mutex_take(s_posix_mutex, OS_WAIT_FOREVER);
    }
}

void posix_unlock(void)
{
    if (s_posix_mutex)
    {
        os_mutex_give(s_posix_mutex);
    }
}

bool posix_port_in_isr(void)
{
    return k_is_in_isr();
}

void posix_port_lock_init(void)
{
    os_mutex_create(&s_posix_mutex);
}

int posix_port_init_all(void)
{
    posix_port_lock_init();
    posix_auto_init();
    return 0;
}
