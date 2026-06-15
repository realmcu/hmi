#ifndef POSIX_IOCTL_ADC_H
#define POSIX_IOCTL_ADC_H

#include "../posix_ioctl.h"
#include "../posix_device.h"

#define POSIX_ADC_RES_8BIT   8
#define POSIX_ADC_RES_10BIT  10
#define POSIX_ADC_RES_12BIT  12
#define POSIX_ADC_RES_14BIT  14
#define POSIX_ADC_RES_16BIT  16

#define POSIX_ADC_REF_INTERNAL  0
#define POSIX_ADC_REF_EXTERNAL  1
#define POSIX_ADC_REF_VDDA      2

/* 配置结构体 */
typedef struct {
    uint32_t resolution;       /* 8/10/12/14/16 */
    uint8_t  reference;
    uint32_t sample_rate_hz;
} posix_adc_config_t;

/* 连续模式 */
typedef struct {
    int     channel;
    void  (*callback)(posix_fd_t fd, const uint32_t *data,
                      int num_samples, void *arg);
    void   *arg;
} posix_adc_continuous_t;

/* ioctl 命令 */
#define POSIX_ADC_IOCTL_SET_CONFIG          POSIX_IOC(POSIX_DEVICE_MAGIC_ADC, 1)
#define POSIX_ADC_IOCTL_GET_CONFIG          POSIX_IOC(POSIX_DEVICE_MAGIC_ADC, 2)
#define POSIX_ADC_IOCTL_READ_CHANNEL        POSIX_IOC(POSIX_DEVICE_MAGIC_ADC, 3)
#define POSIX_ADC_IOCTL_READ_CHANNEL_MV     POSIX_IOC(POSIX_DEVICE_MAGIC_ADC, 4)
#define POSIX_ADC_IOCTL_READ_MULTI          POSIX_IOC(POSIX_DEVICE_MAGIC_ADC, 5)
#define POSIX_ADC_IOCTL_START_CONTINUOUS    POSIX_IOC(POSIX_DEVICE_MAGIC_ADC, 6)
#define POSIX_ADC_IOCTL_STOP_CONTINUOUS     POSIX_IOC(POSIX_DEVICE_MAGIC_ADC, 7)

#endif
