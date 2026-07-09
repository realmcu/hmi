// Zephyr Shell test: uart:~$ posix_gsensor
/* ================================================================
 * G-sensor usage example
 *
 * Function: Open G-sensor -> configure range -> read 3-axis data -> read temperature -> close
 *
 * Build requirements: posix.h + posix_ioctl_gsensor.h
 * ================================================================ */

#include "posix.h"
#include "ioctls/posix_ioctl_gsensor.h"

#ifdef CONFIG_SHELL
#include <zephyr/shell/shell.h>
static int cmd_gsensor_read(const struct shell *sh, size_t argc, char **argv)
{
    shell_print(sh, "gsensor: not yet implemented (stub driver)");
    shell_print(sh, "TODO: implement with RTK I2C API when gsensor driver is available");
    return 0;
}
SHELL_CMD_REGISTER(posix_gsensor, NULL, "POSIX gsensor test (stub)", cmd_gsensor_read);
#endif /* CONFIG_SHELL */

void example_gsensor(void)
{
    /* === 1. Open === */
    posix_fd_t gs = posix_open("/dev/gsensor0");
    if (!gs) { return; }

    /* === 2. Configure range +/-2G, 100Hz === */
    posix_gsensor_config_t cfg =
    {
        .range     = POSIX_GSENSOR_RANGE_2G,
        .odr_hz    = 100,
        .low_power = 0,
    };
    posix_ioctl(gs, POSIX_GSENSOR_IOCTL_SET_CONFIG, &cfg);

    /* === 3. Read 3-axis acceleration === */
    posix_gsensor_axis_t accel;
    posix_read(gs, &accel, sizeof(accel));
    /* accel.x/y/z unit mg (1/1000 g) */

    /* === 4. Read chip temperature === */
    int temp;
    posix_ioctl(gs, POSIX_GSENSOR_IOCTL_READ_TEMP, &temp);
    /* temp unit 0.1 deg C, e.g. 250 = 25.0 deg C */

    /* === 5. Self-test === */
    int ret = posix_ioctl(gs, POSIX_GSENSOR_IOCTL_SELF_TEST, NULL);
    /* ret == 0 means normal */

    /* === 6. Close === */
    posix_close(gs);
    (void)ret;
}
