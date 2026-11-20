/* ================================================================
 * GPIO usage example (pin-level fd)
 *
 * Function: open GPIO0 pin16 (P2_1 LED) and GPIO1 pin31 (P8_5 button)
 *       → configure direction → read/write level → configure interrupt → close
 *
 * Pin-level fd path format: /dev/gpio<N>/p<Pin>
 *   e.g. /dev/gpio0/p16  → P2_1 (GPIOA16), LED
 *       /dev/gpio1/p31  → P8_5 (GPIOB31), button
 *
 * Build requirements: posix.h + posix_ioctl_gpio.h
 * ================================================================ */

#include "posix.h"
#include "ioctls/posix_ioctl_gpio.h"

/* Button interrupt callback */
static void on_button_press(void *arg)
{
    /* ISR context! No blocking allowed */
    /* Usually signal a semaphore to notify a task to handle it */
    (void)arg;
}

void example_gpio(void)
{
    /* === 1. Open the pins === */
    posix_fd_t led = posix_open("/dev/gpio0/p16");   /* P2_1 → GPIOA16, LED */
    posix_fd_t btn = posix_open("/dev/gpio1/p31");   /* P8_5 → GPIOB31, button */
    if (!led || !btn)
    {
        return;
    }

    /* === 2. Configure the LED (output) === */
    posix_gpio_config_t led_cfg =
    {
        .direction     = POSIX_GPIO_DIR_OUTPUT,
        .pull          = POSIX_GPIO_PULL_NONE,
        .initial_value = 0,       /* initial low level (LED off) */
    };
    posix_ioctl(led, POSIX_GPIO_IOCTL_SET_DIR, &led_cfg);

    /* === 3. Configure the button (input + pull-up) === */
    posix_gpio_config_t btn_cfg =
    {
        .direction = POSIX_GPIO_DIR_INPUT,
        .pull      = POSIX_GPIO_PULL_UP,
    };
    posix_ioctl(btn, POSIX_GPIO_IOCTL_SET_DIR, &btn_cfg);

    /* === 4. Write the level (method one: posix_write) === */
    int val = 1;
    posix_write(led, &val, sizeof(val));   /* LED on */

    /* === 5. Write the level (method two: ioctl) === */
    posix_gpio_value_t v = { .value = 0 };
    posix_ioctl(led, POSIX_GPIO_IOCTL_SET_VALUE, &v);  /* LED off */

    /* === 6. Read the level === */
    posix_ioctl(btn, POSIX_GPIO_IOCTL_GET_VALUE, &v);
    /* v.value = 0 or 1 */

    /* Or use posix_read */
    int btn_val;
    posix_read(btn, &btn_val, sizeof(btn_val));

    /* === 7. Configure the interrupt === */
    posix_gpio_irq_t irq =
    {
        .pin      = 0,            /* fd is already bound to a pin; put 0 here */
        .trigger  = POSIX_GPIO_INT_FALLING,   /* falling-edge trigger */
        .callback = on_button_press,
        .arg      = NULL,
    };
    posix_ioctl(btn, POSIX_GPIO_IOCTL_SET_IRQ, &irq);
    posix_ioctl(btn, POSIX_GPIO_IOCTL_ENABLE_IRQ, NULL);

    /* === 8. Close === */
    posix_close(led);
    posix_close(btn);
}

#ifdef CONFIG_SHELL
#include <zephyr/shell/shell.h>
#include <zephyr/logging/log.h>
#include "posix_port.h"
LOG_MODULE_REGISTER(gpio_demo, LOG_LEVEL_DBG);
static bool s_gpio_inited = false;

/* ---------- LED blink thread ---------- */
static K_THREAD_STACK_DEFINE(s_blink_stack, 512);
static struct k_thread s_blink_thread;
static bool s_blink_running = false;

static void blink_task(void *p1, void *p2, void *p3)
{
    (void)p2; (void)p3;
    posix_fd_t led = (posix_fd_t)p1;
    posix_gpio_value_t v = {0};

    while (s_blink_running)
    {
        v.value = 1;
        posix_ioctl(led, POSIX_GPIO_IOCTL_SET_VALUE, &v);
        k_msleep(500);
        v.value = 0;
        posix_ioctl(led, POSIX_GPIO_IOCTL_SET_VALUE, &v);
        k_msleep(500);
    }

    posix_close(led);
}

/* ---------- Button interrupt callback ---------- */
static posix_fd_t s_btn_fd = POSIX_FD_NULL;
static atomic_t   s_btn_press_count;

static void btn_isr(void *arg)
{
    (void)arg;
    int count = (int)atomic_inc(&s_btn_press_count) + 1;
    /* LOG_INF is ISR-safe in deferred mode (writes only to a ring buffer);
     * if the project enables CONFIG_LOG_MODE_IMMEDIATE it must not be called from an ISR. */
    LOG_INF("P8_5 pressed, count=%d", count);
}

/* ---------- shell commands ----------
 * posix_gpio        — start LED blinking + register button interrupt
 * posix_gpio stop   — stop blinking + remove button interrupt
 * posix_gpio btn    — print the cumulative button press count
 */
static int cmd_gpio_test(const struct shell *sh, size_t argc, char **argv)
{
    if (!s_gpio_inited) { posix_port_init_all(); s_gpio_inited = true; }

    if (argc > 1 && strcmp(argv[1], "stop") == 0)
    {
        s_blink_running = false;
        if (s_btn_fd != POSIX_FD_NULL)
        {
            posix_ioctl(s_btn_fd, POSIX_GPIO_IOCTL_DISABLE_IRQ, NULL);
            posix_close(s_btn_fd);
            s_btn_fd = POSIX_FD_NULL;
        }
        shell_print(sh, "stopped");
        return 0;
    }

    if (argc > 1 && strcmp(argv[1], "btn") == 0)
    {
        shell_print(sh, "P8_5 press count = %d", (int)atomic_get(&s_btn_press_count));
        return 0;
    }

    if (s_blink_running)
    {
        shell_print(sh, "already running, use 'posix_gpio stop' to stop");
        return 0;
    }

    /* --- LED P2_1 --- */
    posix_fd_t led3 = posix_open("/dev/gpio0/p16");
    if (led3 == POSIX_FD_NULL) { shell_error(sh, "open /dev/gpio0/p16 failed"); return -1; }

    posix_gpio_config_t led_cfg =
    {
        .direction     = POSIX_GPIO_DIR_OUTPUT,
        .pull          = POSIX_GPIO_PULL_NONE,
        .initial_value = 0,
    };
    posix_ioctl(led3, POSIX_GPIO_IOCTL_SET_DIR, &led_cfg);

    s_blink_running = true;
    k_thread_create(&s_blink_thread, s_blink_stack, K_THREAD_STACK_SIZEOF(s_blink_stack),
                    blink_task, (void *)led3, NULL, NULL,
                    K_PRIO_PREEMPT(10), 0, K_NO_WAIT);
    k_thread_name_set(&s_blink_thread, "blink");

    /* --- Button P8_5 --- */
    s_btn_fd = posix_open("/dev/gpio1/p31");
    if (s_btn_fd == POSIX_FD_NULL)
    {
        shell_error(sh, "open /dev/gpio1/p31 failed");
    }
    else
    {
        posix_gpio_config_t btn_cfg =
        {
            .direction = POSIX_GPIO_DIR_INPUT,
            .pull      = POSIX_GPIO_PULL_UP,
        };
        posix_ioctl(s_btn_fd, POSIX_GPIO_IOCTL_SET_DIR, &btn_cfg);

        atomic_set(&s_btn_press_count, 0);
        posix_gpio_irq_t irq =
        {
            .trigger  = POSIX_GPIO_INT_FALLING,
            .callback = btn_isr,
            .arg      = NULL,
        };
        posix_ioctl(s_btn_fd, POSIX_GPIO_IOCTL_SET_IRQ, &irq);
        posix_ioctl(s_btn_fd, POSIX_GPIO_IOCTL_ENABLE_IRQ, NULL);
    }

    shell_print(sh,
                "P2_1 blinking, P8_5 IRQ armed. 'posix_gpio btn' to check, 'posix_gpio stop' to stop");
    return 0;
}
SHELL_CMD_REGISTER(posix_gpio, NULL, "posix_gpio [stop|btn]", cmd_gpio_test);
#endif /* CONFIG_SHELL */
