/**
 * @file posix_port_init.c
 * @brief POSIX port initialization for RTK8773G + Zephyr
 */

#include "posix_port.h"
#include "os_sync.h"          /* os_mutex_create/take/give 原型 */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

/* RTK OS mutex handle（底层是 Zephyr k_mutex，owner 递归） */
static void *s_posix_mutex = NULL;
static bool  s_posix_init_attempted = false;
static int   s_posix_init_result = POSIX_ERR;

/* os_mutex_take 的“永久等待”哨兵值（osif_zephyr 映射为 K_FOREVER） */
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

/* 框架原型为 int posix_port_in_isr(void)，统一返回 0/1 */
int posix_port_in_isr(void)
{
    return k_is_in_isr() ? 1 : 0;
}

void posix_port_lock_init(void)
{
    if (s_posix_mutex == NULL)
    {
        os_mutex_create(&s_posix_mutex);
    }
}

void *posix_sem_create(const char *name, uint32_t init_count, uint32_t max_count)
{
    void *sem = NULL;
    os_sem_create(&sem, name, init_count, max_count);
    return sem;
}

void posix_sem_delete(void *sem)
{
    if (sem)
    {
        os_sem_delete(sem);
    }
}

int posix_sem_give(void *sem)
{
    if (!sem) { return -1; }
    return os_sem_give(sem) ? 0 : -1;
}

int posix_sem_take(void *sem, uint32_t timeout_ms)
{
    if (!sem) { return -1; }
    return os_sem_take(sem, timeout_ms) ? 0 : -1;
}

void posix_port_delay_ms(uint32_t ms)
{
    k_msleep((int32_t)ms);
}

int posix_port_init_all(void)
{
    posix_port_lock_init();

    posix_lock();
    if (!s_posix_init_attempted)
    {
        s_posix_init_attempted = true;
        s_posix_init_result = posix_auto_init();
        if (s_posix_init_result != POSIX_OK)
        {
            /* 初始化函数可能已经注册了部分设备，禁止自动重试，避免重复初始化
             * FlashDB 或重复注册设备。重试应由显式的恢复流程完成。 */
            printk("[posix] auto_init failed: some device init returned error (ret=%d)\n",
                   s_posix_init_result);
        }
    }
    int ret = s_posix_init_result;
    posix_unlock();
    return ret;
}
