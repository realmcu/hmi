/* ================================================================
 * 自动注册实现
 *
 * 段首尾标记由编译器放在 .posix$initS 和 .posix$initE 段。
 * 所有 POSIX_INIT_*_EXPORT(fn) 注册的 init 函数指针
 * 被链接器收集在 .posix$init0 ~ .posix$init2 段中，
 * 位于首尾标记之间。posix_auto_init() 遍历并调用。
 * ================================================================ */

#include "posix_init.h"
#include "posix_device.h"

static const posix_init_fn_t __posix_init_start[]
    POSIX_SECTION(".posix$initS") POSIX_USED POSIX_ALIGN = { 0 };

static const posix_init_fn_t __posix_init_end[]
    POSIX_SECTION(".posix$initE") POSIX_USED POSIX_ALIGN = { 0 };

int posix_auto_init(void)
{
    int ok = 0, total = 0;

    for (const posix_init_fn_t *p = __posix_init_start + 1;
         p < __posix_init_end; p++) {
        if (*p) {
            total++;
            if ((*p)() == POSIX_OK) ok++;
        }
    }

    return (ok == total) ? POSIX_OK : POSIX_ERR_IO;
}
