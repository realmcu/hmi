/* ================================================================
 * Auto-registration implementation
 *
 * The compiler places start/end markers in the .posix$initS and .posix$initE sections.
 * All init function pointers registered by POSIX_INIT_*_EXPORT(fn)
 * are collected by the linker in sections .posix$init0 ~ .posix$init2,
 * located between the two markers. posix_auto_init() walks and calls them.
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
