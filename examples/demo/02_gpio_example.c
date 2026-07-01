/* ================================================================
 * GPIO 使用示例（引脚级 fd）
 *
 * 功能：打开 GPIO0 的 pin16（P2_1 LED）和 GPIO1 的 pin31（P8_5 按键）
 *       → 配置方向 → 读写电平 → 配置中断 → 关闭
 *
 * 引脚级 fd 路径格式：/dev/gpio<N>/p<Pin>
 *   例：/dev/gpio0/p16  → P2_1 (GPIOA16)，LED
 *       /dev/gpio1/p31  → P8_5 (GPIOB31)，按键
 *
 * 编译要求：需要 posix.h + posix_ioctl_gpio.h
 * ================================================================ */

#include "posix.h"
#include "ioctls/posix_ioctl_gpio.h"

/* 按键中断回调 */
static void on_button_press(void *arg)
{
    /* ISR 上下文！不能阻塞 */
    /* 通常发信号量通知任务去处理 */
    (void)arg;
}

void example_gpio(void)
{
    /* === 1. 打开引脚 === */
    posix_fd_t led = posix_open("/dev/gpio0/p16");   /* P2_1 → GPIOA16，LED */
    posix_fd_t btn = posix_open("/dev/gpio1/p31");   /* P8_5 → GPIOB31，按键 */
    if (!led || !btn)
    {
        return;
    }

    /* === 2. 配置 LED（输出） === */
    posix_gpio_config_t led_cfg =
    {
        .direction     = POSIX_GPIO_DIR_OUTPUT,
        .pull          = POSIX_GPIO_PULL_NONE,
        .initial_value = 0,       /* 初始低电平（LED灭） */
    };
    posix_ioctl(led, POSIX_GPIO_IOCTL_SET_DIR, &led_cfg);

    /* === 3. 配置按键（输入 + 上拉） === */
    posix_gpio_config_t btn_cfg =
    {
        .direction = POSIX_GPIO_DIR_INPUT,
        .pull      = POSIX_GPIO_PULL_UP,
    };
    posix_ioctl(btn, POSIX_GPIO_IOCTL_SET_DIR, &btn_cfg);

    /* === 4. 写电平（方式一：posix_write） === */
    int val = 1;
    posix_write(led, &val, sizeof(val));   /* LED 亮 */

    /* === 5. 写电平（方式二：ioctl） === */
    posix_gpio_value_t v = { .value = 0 };
    posix_ioctl(led, POSIX_GPIO_IOCTL_SET_VALUE, &v);  /* LED 灭 */

    /* === 6. 读电平 === */
    posix_ioctl(btn, POSIX_GPIO_IOCTL_GET_VALUE, &v);
    /* v.value = 0 或 1 */

    /* 或者用 posix_read */
    int btn_val;
    posix_read(btn, &btn_val, sizeof(btn_val));

    /* === 7. 配置中断 === */
    posix_gpio_irq_t irq =
    {
        .pin      = 0,            /* fd 已绑定 pin，这里填 0 */
        .trigger  = POSIX_GPIO_INT_FALLING,   /* 下降沿触发 */
        .callback = on_button_press,
        .arg      = NULL,
    };
    posix_ioctl(btn, POSIX_GPIO_IOCTL_SET_IRQ, &irq);
    posix_ioctl(btn, POSIX_GPIO_IOCTL_ENABLE_IRQ, NULL);

    /* === 8. 关闭 === */
    posix_close(led);
    posix_close(btn);
}

#ifdef CONFIG_SHELL
#include <zephyr/shell/shell.h>
#include <zephyr/logging/log.h>
#include "posix_port.h"
LOG_MODULE_REGISTER(gpio_demo, LOG_LEVEL_DBG);
static bool s_gpio_inited = false;

/* ---------- LED 闪烁线程 ---------- */
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

/* ---------- 按键中断回调 ---------- */
static posix_fd_t s_btn_fd = POSIX_FD_NULL;
static atomic_t   s_btn_press_count;

static void btn_isr(void *arg)
{
    (void)arg;
    int count = (int)atomic_inc(&s_btn_press_count) + 1;
    /* LOG_INF 在 deferred 模式下 ISR 安全（只写环形缓冲）；
     * 若工程开启 CONFIG_LOG_MODE_IMMEDIATE 则不能在 ISR 里调用。 */
    LOG_INF("P8_5 pressed, count=%d", count);
}

/* ---------- shell 命令 ----------
 * posix_gpio        — 开始 LED 闪烁 + 注册按键中断
 * posix_gpio stop   — 停止闪烁 + 摘除按键中断
 * posix_gpio btn    — 打印按键累计按下次数
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

    /* --- 按键 P8_5 --- */
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
