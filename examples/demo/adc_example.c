/* ================================================================
 * ADC usage example
 *
 * Function: open ADC0 → configure 12-bit → single sample → continuous sampling → close
 *
 * Build requirements: posix.h + posix_ioctl_adc.h
 * ================================================================ */

#include "posix.h"
#include "ioctls/posix_ioctl_adc.h"

/* Continuous sampling callback */
static void on_adc_data(posix_fd_t fd, const uint32_t *data,
                        int num_samples, void *arg)
{
    /* ISR or DMA-complete callback context */
    /* data[0] ~ data[num_samples-1] are the sample values */
    (void)fd;
    (void)arg;
}

void example_adc(void)
{
    /* === 1. Open the device === */
    posix_fd_t adc = posix_open("/dev/adc0");
    if (!adc)
    {
        return;
    }

    /* === 2. Configure === */
    posix_adc_config_t cfg =
    {
        .resolution     = 12,               /* 12-bit */
        .reference      = POSIX_ADC_REF_VDDA,
        .sample_rate_hz = 1000,             /* 1KHz */
    };
    posix_ioctl(adc, POSIX_ADC_IOCTL_SET_CONFIG, &cfg);

    /* === 3. Method one: single sample (channel given via ioctl) === */
    uint32_t raw;
    int channel = 0;
    posix_ioctl(adc, POSIX_ADC_IOCTL_READ_CHANNEL, &channel);
    /* raw holds the sample value */
    /* convert to voltage: float mv = (float)raw * 3300.0f / 4096.0f; */

    /* === 4. Method two: single sample (posix_read default channel 0) === */
    posix_read(adc, &raw, sizeof(raw));

    /* === 5. Method three: continuous sampling (DMA) === */
    posix_adc_continuous_t cont =
    {
        .channel  = 0,
        .callback = on_adc_data,
        .arg      = NULL,
    };
    posix_ioctl(adc, POSIX_ADC_IOCTL_START_CONTINUOUS, &cont);

    /* while sampling continues, on_adc_data is called periodically... */

    posix_ioctl(adc, POSIX_ADC_IOCTL_STOP_CONTINUOUS, NULL);

    /* === 6. Close === */
    posix_close(adc);
}

#ifdef CONFIG_SHELL
#include <zephyr/shell/shell.h>
#include "posix_port.h"
static bool s_adc_inited = false;

static int cmd_adc_test(const struct shell *sh, size_t argc, char **argv)
{
    if (!s_adc_inited) { posix_port_init_all(); s_adc_inited = true; }
    posix_fd_t fd = posix_open("/dev/adc0");
    if (fd == POSIX_FD_NULL) { shell_print(sh, "adc0 not registered — no port driver yet"); return 0; }
    shell_print(sh, "open OK");
    posix_close(fd);
    return 0;
}
SHELL_CMD_REGISTER(posix_adc, NULL, "POSIX ADC smoke test", cmd_adc_test);
#endif /* CONFIG_SHELL */
