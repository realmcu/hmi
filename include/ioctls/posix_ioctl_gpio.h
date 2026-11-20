#ifndef POSIX_IOCTL_GPIO_H
#define POSIX_IOCTL_GPIO_H

#include "../posix_ioctl.h"
#include "../posix_device.h"

/* GPIO direction */
#define POSIX_GPIO_DIR_INPUT        0
#define POSIX_GPIO_DIR_OUTPUT       1
#define POSIX_GPIO_DIR_OPEN_DRAIN   2

/* Pull-up/pull-down */
#define POSIX_GPIO_PULL_NONE  0
#define POSIX_GPIO_PULL_UP    1
#define POSIX_GPIO_PULL_DOWN  2

/* Interrupt trigger */
#define POSIX_GPIO_INT_DISABLE      0
#define POSIX_GPIO_INT_RISING       1
#define POSIX_GPIO_INT_FALLING      2
#define POSIX_GPIO_INT_BOTH         3
#define POSIX_GPIO_INT_LOW_LEVEL    4
#define POSIX_GPIO_INT_HIGH_LEVEL   5

/* Pin-level fd → configure direction
 *
 * pin field semantics (cross-platform):
 *   0  = fd already bound to pin_index; only software-level direction/pull/initial value, no pinmux/pad
 *   >0 = chip global pin number (e.g. P3_5=27 on rtl87x3g); the port layer uses it for full
 *        Pinmux/Pad configuration. The global pin number for a given pin_index is decided by board wiring,
 *        so the application must supply it once. */
typedef struct
{
    int     pin;         /* 0 or chip global pin number */
    uint8_t direction;
    uint8_t pull;
    int     initial_value;   /* valid for output */
} posix_gpio_config_t;

/* Pin value */
typedef struct
{
    int pin;
    int value;
} posix_gpio_value_t;

/* Interrupt config */
typedef struct
{
    int     pin;
    uint8_t trigger;       /* POSIX_GPIO_INT_XXX */
    void (*callback)(void *arg);
    void   *arg;
} posix_gpio_irq_t;

/* Bulk operations */
typedef struct
{
    uint32_t pin_mask;
    uint32_t values;
} posix_gpio_multi_t;

/* ioctl commands */
#define POSIX_GPIO_IOCTL_SET_DIR        POSIX_IOC(POSIX_DEVICE_MAGIC_GPIO, 1)
#define POSIX_GPIO_IOCTL_SET_PULL       POSIX_IOC(POSIX_DEVICE_MAGIC_GPIO, 2)
#define POSIX_GPIO_IOCTL_SET_VALUE      POSIX_IOC(POSIX_DEVICE_MAGIC_GPIO, 3)
#define POSIX_GPIO_IOCTL_GET_VALUE      POSIX_IOC(POSIX_DEVICE_MAGIC_GPIO, 4)
#define POSIX_GPIO_IOCTL_SET_MULTI      POSIX_IOC(POSIX_DEVICE_MAGIC_GPIO, 5)
#define POSIX_GPIO_IOCTL_GET_MULTI      POSIX_IOC(POSIX_DEVICE_MAGIC_GPIO, 6)
#define POSIX_GPIO_IOCTL_SET_IRQ        POSIX_IOC(POSIX_DEVICE_MAGIC_GPIO, 7)
#define POSIX_GPIO_IOCTL_ENABLE_IRQ     POSIX_IOC(POSIX_DEVICE_MAGIC_GPIO, 8)
#define POSIX_GPIO_IOCTL_DISABLE_IRQ    POSIX_IOC(POSIX_DEVICE_MAGIC_GPIO, 9)

#endif
