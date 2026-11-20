#ifndef POSIX_INIT_H
#define POSIX_INIT_H

/* ================================================================
 * Auto-registration mechanism
 *
 * Uses GCC __attribute__((section)) to place init function pointers
 * into dedicated ELF sections; startup walks it and calls each one.
 *
 * Priority levels:
 *   0 → board init (clock, pinmux)
 *   1 → device driver registration (UART/SPI/GPIO/...)
 *   1_5 → post-registration init (cross-device binding/selection)
 *   2 → application init
 *
 * Usage: at the end of each driver .c file:
 *
 *   static int uart_init(void) {
 *       return posix_device_register("/dev/uart0", &ops, &priv);
 *   }
 *   POSIX_INIT_DEVICE_EXPORT(uart_init);
 *
 * The linker script must collect these sections in order (sentinels at both ends):
 *
 *   .posix_init : {
 *       KEEP(*(.posix$initS))     // start sentinel (provided by posix_init.c)
 *       KEEP(*(.posix$init0))     // priority 0: board (clock/pinmux)
 *       KEEP(*(.posix$init1))     // priority 1: device driver registration
 *       KEEP(*(.posix$init1_5))   // priority 1_5: post-registration init
 *       KEEP(*(.posix$init2))     // priority 2: application
 *       KEEP(*(.posix$initE))     // end sentinel (provided by posix_init.c)
 *   } > FLASH
 *
 * Important:
 *  - Iteration bounds come from the C array sentinels in posix_init.c
 *    (__posix_init_start[] / __posix_init_end[]);
 *    do NOT define __posix_init_start/end linker symbols in the script.
 *  - The '$' in section names is a plain MSVC/IAR-style char; GNU ld does not
 *    sort by 0/1/2 automatically, so init0/init1/init1_5/init2 must be listed
 *    explicitly as above; wildcard KEEP(*(.posix$init*)) loses priority (order not guaranteed).
 *  - Reference implementation: examples/port/posix_init_zephyr.ld.
 * ================================================================ */

#include <stdint.h>

typedef int (*posix_init_fn_t)(void);

#define POSIX_USED          __attribute__((used))
#define POSIX_SECTION(x)    __attribute__((section(x)))
#define POSIX_ALIGN         __attribute__((aligned(sizeof(void *))))

#define POSIX_INIT_EXPORT(fn, level)                                           \
    POSIX_USED const posix_init_fn_t __posix_init_##fn                         \
    POSIX_SECTION(".posix$init" level) POSIX_ALIGN = fn

/* Priority levels */
#define POSIX_INIT_BOARD_EXPORT(fn)     POSIX_INIT_EXPORT(fn, "0")
#define POSIX_INIT_DEVICE_EXPORT(fn)    POSIX_INIT_EXPORT(fn, "1")
#define POSIX_INIT_POST_DEVICE_EXPORT(fn) POSIX_INIT_EXPORT(fn, "1_5")
#define POSIX_INIT_APP_EXPORT(fn)       POSIX_INIT_EXPORT(fn, "2")

/* Walk all registered init functions and call them */
int posix_auto_init(void);

#endif /* POSIX_INIT_H */
