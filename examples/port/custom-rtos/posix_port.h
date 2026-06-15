#ifndef POSIX_PORT_H
#define POSIX_PORT_H

/* ================================================================
 * 平台移植接口
 *
 * 移植到新 RTOS 时需要实现的函数：
 *   1. posix_lock() / posix_unlock()  — 多线程保护
 *   2. posix_port_in_isr()            — ISR 上下文检测
 *
 * 每个驱动通过 POSIX_INIT_DEVICE_EXPORT(fn) 自动注册
 * 无需在 port 层显式调用注册函数
 * ================================================================ */

#include "posix.h"
#include "posix_init.h"

/* 一次性初始化：锁 + 自动注册所有设备 */
int posix_port_init_all(void);

/* 内部锁初始化 */
void posix_port_lock_init(void);

/* ================================================================
 * 静态内存池模式（推荐用于无堆嵌入式环境）
 *
 * 每个驱动需要为 open 分配 file_priv 结构体。
 * 在无堆环境中，建议使用静态池代替 malloc：
 *
 *   #define MAX_FILES  4
 *   static my_file_t s_pool[MAX_FILES];
 *   static int s_used[MAX_FILES];
 *
 *   static void *my_open(void *drv_data, const char *path) {
 *       for (int i = 0; i < MAX_FILES; i++) {
 *           if (!s_used[i]) { s_used[i] = 1; return &s_pool[i]; }
 *       }
 *       return NULL;  // 池满
 *   }
 *
 *   static int my_close(void *drv_data, void *file_priv) {
 *       my_file_t *f = (my_file_t *)file_priv;
 *       int idx = f - s_pool;  // 指针减法得索引
 *       if (idx >= 0 && idx < MAX_FILES) s_used[idx] = 0;
 *       return POSIX_OK;
 *   }
 *
 * 各驱动具体池大小可参考 PORTING.md 中最大同时打开数。
 * ================================================================ */

#endif /* POSIX_PORT_H */
