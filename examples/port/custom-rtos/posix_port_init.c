/* ================================================================
 * 平台初始化 — 移植到你的 RTOS 时只需修改此文件
 *
 * 步骤（详见 PORTING.md 第一步）：
 *   1. 包含你的 RTOS 同步原语头文件
 *   2. 实现 posix_lock / posix_unlock（全局互斥锁，保护框架元数据）
 *   3. 实现 posix_port_in_isr（ISR 上下文判断，返回非 0 = 在 ISR 中）
 *   4. 在 posix_port_lock_init 中创建互斥锁
 *
 * 单线程/裸机场景：posix_lock/unlock 留空即可，posix_port_in_isr 恒返回 0。
 * ================================================================ */

#include "posix_port.h"

/* --- 包含你的 RTOS 同步原语头文件 ---
 * #include "your_rtos.h"
 */

/* --- 互斥锁句柄（由 posix_port_lock_init 创建）---
 * static your_mutex_t s_posix_mutex;
 */

void posix_lock(void)
{
    /* 多线程 RTOS：
     * your_rtos_mutex_lock(&s_posix_mutex); */
}

void posix_unlock(void)
{
    /* 多线程 RTOS：
     * your_rtos_mutex_unlock(&s_posix_mutex); */
}

int posix_port_in_isr(void)
{
    /* 大多数 RTOS 有现成接口：
     * return your_rtos_in_isr();
     *
     * 如果没有，可在进入/退出 ISR 时维护全局标志：
     * extern volatile int g_in_isr;
     * return g_in_isr; */
    return 0;
}

void posix_port_lock_init(void)
{
    /* 多线程 RTOS：
     * your_rtos_mutex_create(&s_posix_mutex); */
}

void *posix_sem_create(const char *name, uint32_t init_count, uint32_t max_count)
{
    /* 替换为你的 RTOS 实现，例如：
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

int posix_port_init_all(void)
{
    posix_port_lock_init();
    return posix_auto_init();
}
