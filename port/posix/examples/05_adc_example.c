/* ================================================================
 * ADC 使用示例
 *
 * 功能：打开 ADC0 → 配置 12 位 → 单次采样 → 连续采样 → 关闭
 *
 * 编译要求：需要 posix.h + posix_ioctl_adc.h
 * ================================================================ */

#include "posix.h"
#include "ioctls/posix_ioctl_adc.h"

/* 连续采样回调 */
static void on_adc_data(posix_fd_t fd, const uint32_t *data,
                        int num_samples, void *arg)
{
    /* ISR 或 DMA 完成回调上下文 */
    /* data[0] ~ data[num_samples-1] 是采样值 */
    (void)fd;
    (void)arg;
}

void example_adc(void)
{
    /* === 1. 打开设备 === */
    posix_fd_t adc = posix_open("/dev/adc0");
    if (!adc)
    {
        return;
    }

    /* === 2. 配置 === */
    posix_adc_config_t cfg =
    {
        .resolution     = 12,               /* 12 位 */
        .reference      = POSIX_ADC_REF_VDDA,
        .sample_rate_hz = 1000,             /* 1KHz */
    };
    posix_ioctl(adc, POSIX_ADC_IOCTL_SET_CONFIG, &cfg);

    /* === 3. 方式一：单次采样（ioctl 指定通道） === */
    uint32_t raw;
    int channel = 0;
    posix_ioctl(adc, POSIX_ADC_IOCTL_READ_CHANNEL, &channel);
    /* raw 中是采样值 */
    /* 换算电压: float mv = (float)raw * 3300.0f / 4096.0f; */

    /* === 4. 方式二：单次采样（posix_read 默认通道 0） === */
    posix_read(adc, &raw, sizeof(raw));

    /* === 5. 方式三：连续采样（DMA） === */
    posix_adc_continuous_t cont =
    {
        .channel  = 0,
        .callback = on_adc_data,
        .arg      = NULL,
    };
    posix_ioctl(adc, POSIX_ADC_IOCTL_START_CONTINUOUS, &cont);

    /* 持续采样中，on_adc_data 会被周期性调用... */

    posix_ioctl(adc, POSIX_ADC_IOCTL_STOP_CONTINUOUS, NULL);

    /* === 6. 关闭 === */
    posix_close(adc);
}
