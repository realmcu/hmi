/* ================================================================
 * Touch layer: current board selection (decides which chip /dev/touch0 maps to)
 *
 * This file contains no hardware access logic; does two things:
 *
 *  1. Provides posix_touch_bind / unbind —— keeps a typed-prefix
 *     API for the touch app layer. The implementation just forwards to the framework's posix_device_bind_alias
 *     (see core/posix_device.c); other multi-model peripherals like gsensor use the same mechanism.
 *
 *  2. Calls bind once at POSIX_INIT_APP_EXPORT time, mounting the board's chip as
 *     /dev/touch0. Upper-layer apps only open /dev/touch0 and do not care whether it is CST816D
 *     or CHSC6417; for debugging you can still open /dev/cst816d directly.
 *
 * Changing boards/chips only touches this file's #if branches (or adds a Kconfig option):
 *     CONFIG_BOARD_TOUCH_CST816D  (default, matching eBadge board)
 *     CONFIG_BOARD_TOUCH_CHSC6417
 * Neither chip driver .c nor the posix framework changes.
 *
 * init section priority note:
 *   The chip driver is in .posix$init1 via POSIX_INIT_DEVICE_EXPORT,
 *   this file's board_touch_init is in .posix$init2 (APP_EXPORT),
 *   and the linker script guarantees bind happens after the chip registers itself.
 * ================================================================ */

#include "posix.h"
#include "posix_init.h"
#include "ioctls/posix_ioctl_touch.h"
#include <stdio.h>

#define TOUCH_CST816D_CHIP_ID   0xB6

/* ---------- Typed thin wrappers (forward to the framework's generic alias mechanism) ---------- */

int posix_touch_bind(const char *alias, const char *chip_path)
{
    return posix_device_bind_alias(alias, chip_path);
}

int posix_touch_unbind(const char *alias)
{
    return posix_device_unbind_alias(alias);
}


static int board_touch_init(void)
{
    posix_fd_t probe = posix_open("/dev/cst816d");
    int ret;

    if (probe == POSIX_FD_NULL)
    {
        printf("[TOUCH] open /dev/cst816d failed\n");
        return POSIX_ERR_NODEV;
    }

    uint32_t chip_id = 0;
    ret = posix_ioctl(probe, POSIX_TOUCH_IOCTL_GET_CHIP_ID, &chip_id);
    posix_close(probe);

    if (ret != POSIX_OK)
    {
        printf("[TOUCH] CST816D read id failed: %d\n", ret);
        return ret;
    }

    printf("[TOUCH] CST816D id: 0x%08X\n", chip_id);

    if (chip_id == TOUCH_CST816D_CHIP_ID)
    {
        return posix_touch_bind("/dev/touch0", "/dev/cst816d");
    }

    printf("[TOUCH] CST816D id mismatch\n");
    return POSIX_ERR_NODEV;
}
POSIX_INIT_POST_DEVICE_EXPORT(board_touch_init);
