/* ================================================================
 * Gsensor layer: current board selection (decides which chip /dev/gsensor0 maps to)
 *
 * Exactly symmetric with posix_port_touch.c; does two things:
 *
 *  1. Provides posix_gsensor_bind / unbind —— typed thin wrappers forwarding to
 *     the framework's generic posix_device_bind_alias (see core/posix_device.c).
 *
 *  2. Calls bind once at POSIX_INIT_APP_EXPORT time, mounting the board's chip as
 *     /dev/gsensor0. Upper-layer apps only open /dev/gsensor0 and do not care which chip is beneath;
 *     for debugging you can still open the chip model path directly (currently /dev/sc7a20).
 *
 * The chip's own driver is posix_port_sc7a20.c in the same directory, registering /dev/sc7a20.
 *
 * Changing boards/chips only touches this file's #if branches (or adds a Kconfig option). To add a model later:
 *   1) Add e.g. posix_port_bma253.c; POSIX_INIT_DEVICE_EXPORT registers
 *      /dev/bma253
 *   2) Add an #elif branch in this file
 * Existing chip driver .c files and the posix framework stay untouched.
 *
 * init section priority:
 *   The chip driver's POSIX_INIT_DEVICE_EXPORT lives in .posix$init1,
 *   this file's board_gsensor_init lives in .posix$init2 (APP_EXPORT),
 *   guaranteeing bind happens after the chip registers itself.
 * ================================================================ */

#include "posix.h"
#include "posix_init.h"
#include "ioctls/posix_ioctl_gsensor.h"

/* ---------- Typed thin wrappers (forward to the framework's generic alias mechanism) ---------- */

int posix_gsensor_bind(const char *alias, const char *chip_path)
{
    return posix_device_bind_alias(alias, chip_path);
}

int posix_gsensor_unbind(const char *alias)
{
    return posix_device_unbind_alias(alias);
}

/* ---------- Current board selection ---------- */

#if defined(CONFIG_BOARD_GSENSOR_BMA253)
#  define BOARD_GSENSOR_CHIP_PATH  "/dev/bma253"
#elif defined(CONFIG_BOARD_GSENSOR_LIS3DH)
#  define BOARD_GSENSOR_CHIP_PATH  "/dev/lis3dh"
#else   /* default SC7A20, matching eBadge board hardware */
#  define BOARD_GSENSOR_CHIP_PATH  "/dev/sc7a20"
#endif

static int board_gsensor_init(void)
{
    return posix_gsensor_bind("/dev/gsensor0", BOARD_GSENSOR_CHIP_PATH);
}
POSIX_INIT_APP_EXPORT(board_gsensor_init);
