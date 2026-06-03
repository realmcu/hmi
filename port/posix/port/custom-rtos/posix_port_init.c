/* ================================================================
 * 平台初始化 — 用户根据实际 RTOS 实现
 *
 * 移植时只需要：
 *   1. 实现 posix_lock/unlock/posix_port_in_isr
 *   2. 在每个驱动 .c 文件尾部加 POSIX_INIT_DEVICE_EXPORT(fn)
 *   3. 链接脚本保留 .posix$init* 段
 * ================================================================ */

#include "posix_port.h"

/* 互斥锁 */
/* #include "your_rtos.h" */
/* static your_mutex_t s_posix_mutex; */

void posix_lock(void)
{
    /* your_rtos_mutex_lock(&s_posix_mutex); */
}

void posix_unlock(void)
{
    /* your_rtos_mutex_unlock(&s_posix_mutex); */
}

int posix_port_in_isr(void)
{
    /* return your_rtos_in_isr(); */
    return 0;
}

void posix_port_lock_init(void)
{
    /* your_rtos_mutex_create(&s_posix_mutex); */
}

int posix_port_init_all(void)
{
    posix_port_lock_init();

    /* 自动遍历所有 POSIX_INIT_DEVICE_EXPORT 注册的设备 */
    return posix_auto_init();
}
