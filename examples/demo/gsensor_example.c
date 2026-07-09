// Zephyr Shell test:
//   posix_gsensor poll [chip_path]
//   posix_gsensor irq  [chip_path]
//   posix_gsensor bind   <alias> <chip_path>
//   posix_gsensor unbind <alias>
/* ================================================================
 * G-sensor usage example (polling & interrupt modes)
 *
 * Data path goes entirely through POSIX abstraction:
 *   /dev/gsensor0 is a board-selected alias; on eBadge it points to
 *   /dev/sc7a20. The chip driver internally does
 *   posix_open(/dev/i2c0) + posix_open(/dev/gpio0/pX).
 *   Application code only opens /dev/gsensor0; debug can open
 *   /dev/sc7a20 directly (same device, different name).
 *
 * Mode selection:
 *   posix_gsensor_config_t.use_irq = 0  -> polling: posix_read non-blocking
 *   posix_gsensor_config_t.use_irq = 1  -> interrupt: DRDY rising edge wakes posix_read
 *
 * !!! Current implementation status (this project links custom-rtos port) !!!
 *   posix_port_i2c.c      - RTL876x native I2C API implementation (actual SC7A20)
 *   posix_port_sc7a20.c   - full logic (SC7A20 complete registers + IRQ blocking read)
 *   posix_port_gpio.c     - still a stub. So with use_irq=1, SET_IRQ returns OK,
 *                           but DRDY interrupt never fires, posix_read always times out.
 *                           To use IRQ mode, implement GPIO port first (Zephyr GPIO
 *                           subsystem or RTL876x native API, pick one).
 *
 *   zephyr-rtk port keeps independent stub copy, unaffected by this project build.
 *
 * Build requirement: need posix.h + posix_ioctl_gsensor.h
 * ================================================================ */

#include "posix.h"
#include "ioctls/posix_ioctl_gsensor.h"

#define GSENSOR_DEFAULT_PATH  "/dev/gsensor0"

/* ----------------- Polling mode (simplest) ----------------- */
void example_gsensor_poll(void)
{
    /* === 1. Open === */
    posix_fd_t gs = posix_open(GSENSOR_DEFAULT_PATH);
    if (!gs) { return; }

    /* === 2. Config range +/-2G, 100Hz, polling === */
    posix_gsensor_config_t cfg =
    {
        .range     = POSIX_GSENSOR_RANGE_2G,
        .odr_hz    = 100,
        .low_power = 0,
        .use_irq   = 0,
    };
    posix_ioctl(gs, POSIX_GSENSOR_IOCTL_SET_CONFIG, &cfg);

    /* === 3. Read 3-axis acceleration (mg) === */
    posix_gsensor_axis_t accel;
    posix_read(gs, &accel, sizeof(accel));

    /* === 4. Self-test (verify WHO_AM_I) === */
    int ret = posix_ioctl(gs, POSIX_GSENSOR_IOCTL_SELF_TEST, NULL);
    /* ret == 0 means OK */

    /* === 5. Close === */
    posix_close(gs);
    (void)accel; (void)ret;
}

/* ----------------- Interrupt mode (recommended for low-overhead reads) -----------------
 * Flow:
 *   SET_CONFIG{use_irq=1} internally:
 *     - posix_open /dev/gpio0/pX to get DRDY pin
 *     - create semaphore, posix_ioctl(SET_IRQ + ENABLE_IRQ)
 *     - write SC7A20 CTRL_REG3 to route DRDY to INT1
 *   posix_read blocks on the internal semaphore until ISR triggers give.
 */
void example_gsensor_irq(void)
{
    posix_fd_t gs = posix_open(GSENSOR_DEFAULT_PATH);
    if (!gs) { return; }

    /* 1. Enable interrupt mode: +/-4G, 50Hz */
    posix_gsensor_config_t cfg =
    {
        .range     = POSIX_GSENSOR_RANGE_4G,
        .odr_hz    = 50,
        .low_power = 0,
        .use_irq   = 1,
    };
    if (posix_ioctl(gs, POSIX_GSENSOR_IOCTL_SET_CONFIG, &cfg) != 0)
    {
        posix_close(gs);
        return;
    }

    /* 2. Set DRDY wait timeout (default 1000ms) */
    uint32_t tmo_ms = 500;
    posix_ioctl(gs, POSIX_GSENSOR_IOCTL_SET_TIMEOUT, &tmo_ms);

    /* 3. Read 16 times - each read blocks until next data-ready interrupt */
    for (int i = 0; i < 16; i++)
    {
        posix_gsensor_axis_t accel;
        posix_ssize_t n = posix_read(gs, &accel, sizeof(accel));
        if (n < 0)
        {
            /* n == POSIX_ERR_TIMEOUT (-3) means interrupt timeout */
            break;
        }
        /* TODO: process accel.x/y/z (mg) */
    }

    /* 4. Switch back to polling mode (teardown IRQ) */
    cfg.use_irq = 0;
    posix_ioctl(gs, POSIX_GSENSOR_IOCTL_SET_CONFIG, &cfg);

    posix_close(gs);
}

#ifdef CONFIG_SHELL
#include <zephyr/shell/shell.h>
#include "posix_port.h"
#include <string.h>

static bool s_gs_inited = false;

static int do_gsensor_poll(const struct shell *sh, const char *path)
{
    posix_fd_t gs = posix_open(path);
    if (gs == POSIX_FD_NULL) { shell_error(sh, "open %s failed", path); return -1; }

    posix_gsensor_config_t cfg =
    {
        .range = POSIX_GSENSOR_RANGE_2G, .odr_hz = 100,
        .low_power = 0, .use_irq = 0,
    };
    posix_ioctl(gs, POSIX_GSENSOR_IOCTL_SET_CONFIG, &cfg);

    int self = posix_ioctl(gs, POSIX_GSENSOR_IOCTL_SELF_TEST, NULL);
    shell_print(sh, "self-test: %s (ret=%d)", (self == 0) ? "OK" : "FAIL", self);

    for (int i = 0; i < 5; i++)
    {
        posix_gsensor_axis_t a = {0};
        posix_ssize_t n = posix_read(gs, &a, sizeof(a));
        if (n < 0)
        {
            shell_error(sh, "read fail %d", (int)n);
            break;
        }
        shell_print(sh, "[poll %d] x=%d y=%d z=%d (mg)", i, a.x, a.y, a.z);
        /* ODR=100Hz -> 10ms/sample; without delay when BDU active, may read same frame */
        posix_port_delay_ms(200);
    }
    posix_close(gs);
    return 0;
}

static int do_gsensor_irq(const struct shell *sh, const char *path)
{
    posix_fd_t gs = posix_open(path);
    if (gs == POSIX_FD_NULL) { shell_error(sh, "open %s failed", path); return -1; }

    posix_gsensor_config_t cfg =
    {
        .range = POSIX_GSENSOR_RANGE_2G, .odr_hz = 100,
        .low_power = 0, .use_irq = 1,
    };
    int r = posix_ioctl(gs, POSIX_GSENSOR_IOCTL_SET_CONFIG, &cfg);
    if (r != 0)
    {
        shell_error(sh, "enable IRQ mode failed: %d (check int_pin_path)", r);
        posix_close(gs);
        return -1;
    }
    uint32_t tmo = 1000;
    posix_ioctl(gs, POSIX_GSENSOR_IOCTL_SET_TIMEOUT, &tmo);

    shell_print(sh, "IRQ mode: reading 5 samples (1s timeout each)...");
    for (int i = 0; i < 5; i++)
    {
        posix_gsensor_axis_t a = {0};
        posix_ssize_t n = posix_read(gs, &a, sizeof(a));
        if (n < 0)
        {
            shell_error(sh, "[irq %d] read fail %d (likely timeout)", i, (int)n);
            break;
        }
        shell_print(sh, "[irq %d] x=%d y=%d z=%d (mg)", i, a.x, a.y, a.z);
    }
    cfg.use_irq = 0;
    posix_ioctl(gs, POSIX_GSENSOR_IOCTL_SET_CONFIG, &cfg);
    posix_close(gs);
    return 0;
}

static int cmd_gsensor(const struct shell *sh, size_t argc, char **argv)
{
    if (!s_gs_inited) { posix_port_init_all(); s_gs_inited = true; }

    /* usage:
     *   posix_gsensor                        -> poll /dev/gsensor0
     *   posix_gsensor irq                    -> irq  /dev/gsensor0
     *   posix_gsensor poll /dev/sc7a20       -> poll a specific chip
     *   posix_gsensor bind /dev/foo /dev/sc7a20
     *   posix_gsensor unbind /dev/foo
     */
    const char *mode = (argc >= 2) ? argv[1] : "poll";

    if (strcmp(mode, "bind") == 0)
    {
        if (argc != 4)
        {
            shell_error(sh, "usage: posix_gsensor bind <alias> <chip_path>");
            return -1;
        }
        int ret = posix_gsensor_bind(argv[2], argv[3]);
        if (ret != POSIX_OK) { shell_error(sh, "bind failed: %d", ret); return -1; }
        shell_print(sh, "bound %s -> %s", argv[2], argv[3]);
        return 0;
    }
    if (strcmp(mode, "unbind") == 0)
    {
        if (argc != 3)
        {
            shell_error(sh, "usage: posix_gsensor unbind <alias>");
            return -1;
        }
        int ret = posix_gsensor_unbind(argv[2]);
        if (ret != POSIX_OK)
        {
            shell_error(sh, "unbind failed: %d (busy? not bound?)", ret);
            return -1;
        }
        shell_print(sh, "unbound %s", argv[2]);
        return 0;
    }

    const char *path = (argc >= 3) ? argv[2] : GSENSOR_DEFAULT_PATH;
    if (strcmp(mode, "irq") == 0)  { return do_gsensor_irq(sh, path);  }
    if (strcmp(mode, "poll") == 0) { return do_gsensor_poll(sh, path); }

    shell_error(sh, "usage: posix_gsensor [poll|irq [path] | bind <alias> <chip> | unbind <alias>]");
    return -1;
}

SHELL_CMD_REGISTER(posix_gsensor, NULL,
                   "POSIX gsensor test  "
                   "(usage: posix_gsensor [poll|irq [path] | bind <alias> <chip> | unbind <alias>])",
                   cmd_gsensor);
#endif /* CONFIG_SHELL */
