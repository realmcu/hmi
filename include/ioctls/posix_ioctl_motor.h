#ifndef POSIX_IOCTL_MOTOR_H
#define POSIX_IOCTL_MOTOR_H

#include <stdint.h>

#include "../posix_ioctl.h"
#include "../posix_device.h"

/* Strength is a percentage. The board driver may clamp it to its own safe
 * electrical limit; callers should not assume that 100 means full rail. */
typedef struct
{
    uint16_t duration_ms;
    uint8_t strength;
} posix_motor_pulse_t;

/* Commands. All pointers passed through arg remain owned by the caller. */
#define POSIX_MOTOR_IOCTL_START          POSIX_IOC(POSIX_DEVICE_MAGIC_MOTOR, 1)
#define POSIX_MOTOR_IOCTL_STOP           POSIX_IOC(POSIX_DEVICE_MAGIC_MOTOR, 2)
#define POSIX_MOTOR_IOCTL_PULSE          POSIX_IOC(POSIX_DEVICE_MAGIC_MOTOR, 3)
#define POSIX_MOTOR_IOCTL_SET_STRENGTH   POSIX_IOC(POSIX_DEVICE_MAGIC_MOTOR, 4)

#endif /* POSIX_IOCTL_MOTOR_H */
