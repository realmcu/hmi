#ifndef POSIX_INIT_H
#define POSIX_INIT_H

/* ================================================================
 * 自动注册机制
 *
 * 利用 GCC __attribute__((section)) 将初始化函数指针
 * 放入特定的 ELF 段。启动时遍历该段，逐个调用。
 *
 * 优先级：
 *   0 → 板级初始化（时钟、pinmux）
 *   1 → 设备驱动注册（UART/SPI/GPIO/...）
 *   2 → 应用初始化
 *
 * 用法：在每个驱动 .c 文件尾部：
 *
 *   static int uart_init(void) {
 *       return posix_device_register("/dev/uart0", &ops, &priv);
 *   }
 *   POSIX_INIT_DEVICE_EXPORT(uart_init);
 *
 * 链接脚本需要把以下段【按顺序】收集到一起（哨兵在两端）：
 *
 *   .posix_init : {
 *       KEEP(*(.posix$initS))     // 起始哨兵（由 posix_init.c 提供）
 *       KEEP(*(.posix$init0))     // 优先级 0：板级（时钟/pinmux）
 *       KEEP(*(.posix$init1))     // 优先级 1：设备驱动注册
 *       KEEP(*(.posix$init2))     // 优先级 2：应用
 *       KEEP(*(.posix$initE))     // 结束哨兵（由 posix_init.c 提供）
 *   } > FLASH
 *
 * 重要：
 *  - 遍历边界来自 posix_init.c 中的 C 数组哨兵
 *    (__posix_init_start[] / __posix_init_end[])，
 *    链接脚本【不要】再自行定义 __posix_init_start/end 链接器符号。
 *  - 段名带 '$' 仅是 MSVC/IAR 风格的普通字符，GNU ld 不会按 0/1/2
 *    自动排序，因此必须像上面那样【逐段显式列出】init0/init1/init2，
 *    不能用通配 KEEP(*(.posix$init*))（顺序不保证，优先级会失效）。
 *  - 参考实现：port/zephyr-rtk/posix_init.ld。
 * ================================================================ */

#include <stdint.h>

typedef int (*posix_init_fn_t)(void);

#define POSIX_USED          __attribute__((used))
#define POSIX_SECTION(x)    __attribute__((section(x)))
#define POSIX_ALIGN         __attribute__((aligned(sizeof(void *))))

#define POSIX_INIT_EXPORT(fn, level)                                           \
    POSIX_USED const posix_init_fn_t __posix_init_##fn                         \
    POSIX_SECTION(".posix$init" level) POSIX_ALIGN = fn

/* 优先级等级 */
#define POSIX_INIT_BOARD_EXPORT(fn)     POSIX_INIT_EXPORT(fn, "0")
#define POSIX_INIT_DEVICE_EXPORT(fn)    POSIX_INIT_EXPORT(fn, "1")
#define POSIX_INIT_APP_EXPORT(fn)       POSIX_INIT_EXPORT(fn, "2")

/* 遍历所有已注册的 init 函数并调用 */
int posix_auto_init(void);

#endif /* POSIX_INIT_H */
