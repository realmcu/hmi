#ifndef POSIX_IOCTL_HRM_H
#define POSIX_IOCTL_HRM_H

#include <stdbool.h>
#include <stdint.h>

#include "../posix_ioctl.h"
#include "../posix_device.h"

#define POSIX_HRM_MAX_SAMPLES 64u

typedef enum
{
    POSIX_HRM_MODE_HR = 0,
    POSIX_HRM_MODE_HRV,
    POSIX_HRM_MODE_SPO2,
} posix_hrm_mode_t;

typedef struct
{
    uint16_t samples[POSIX_HRM_MAX_SAMPLES];
    uint8_t count;
    uint8_t env;
    uint8_t ps;
    uint8_t led_current;
    bool worn;
    bool wear_changed;
} posix_hrm_frame_t;

typedef struct
{
    uint8_t chip_id;
    uint8_t version;
    uint8_t status;
    uint8_t event;
    uint8_t ps;
    uint8_t env;
    bool link_ok;
    bool hr_ok;
    bool spo2_ok;
    uint16_t hr_raw;
    uint16_t spo2_red;
    uint16_t spo2_ir;
    int last_error;
} posix_hrm_status_t;

typedef struct
{
    uint16_t red;
    uint16_t ir;
} posix_hrm_spo2_t;

#define POSIX_HRM_IOCTL_SELF_TEST       POSIX_IOC(POSIX_DEVICE_MAGIC_HRM, 1)
#define POSIX_HRM_IOCTL_OPEN            POSIX_IOC(POSIX_DEVICE_MAGIC_HRM, 2)
#define POSIX_HRM_IOCTL_STOP            POSIX_IOC(POSIX_DEVICE_MAGIC_HRM, 3)
#define POSIX_HRM_IOCTL_READ_FRAME      POSIX_IOC(POSIX_DEVICE_MAGIC_HRM, 4)
#define POSIX_HRM_IOCTL_READ_SPO2       POSIX_IOC(POSIX_DEVICE_MAGIC_HRM, 5)
#define POSIX_HRM_IOCTL_GET_STATUS      POSIX_IOC(POSIX_DEVICE_MAGIC_HRM, 6)

#endif /* POSIX_IOCTL_HRM_H */
