/* ================================================================
 * GPIO 使用示例（引脚级 fd）
 *
 * 功能：打开 GPIO0 的 pin12（LED）和 pin3（按键）
 *       → 配置方向 → 读写电平 → 配置中断 → 关闭
 *
 * 引脚级 fd 路径格式：/dev/gpio<N>/p<Pin>
 *   例：/dev/gpio0/p12   → GPIO 控制器 0 的 pin 12
 *       /dev/gpio1/p0    → GPIO 控制器 1 的 pin 0
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
    posix_fd_t led = posix_open("/dev/gpio0/p12");   /* LED 引脚 */
    posix_fd_t btn = posix_open("/dev/gpio0/p3");    /* 按键引脚 */
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
#include "posix_port.h"
static bool s_gpio_inited = false;

static int cmd_gpio_test(const struct shell *sh, size_t argc, char **argv)
{
    if (!s_gpio_inited) { posix_port_init_all(); s_gpio_inited = true; }

    posix_fd_t fd = posix_open("/dev/gpio0/p0");
    if (fd == POSIX_FD_NULL) { shell_error(sh, "open /dev/gpio0/p0 failed"); return -1; }

    /* Configure pin as input */
    posix_gpio_config_t cfg = {POSIX_GPIO_DIR_INPUT, POSIX_GPIO_PULL_NONE, 0};
    posix_ioctl(fd, POSIX_GPIO_IOCTL_SET_DIR, &cfg);

    /* Read pin value */
    int val = 0;
    posix_read(fd, &val, sizeof(val));
    shell_print(sh, "gpio0/p0 value = %d", val);

    posix_close(fd);
    shell_print(sh, "POSIX GPIO test PASSED");
    return 0;
}
SHELL_CMD_REGISTER(posix_gpio, NULL, "POSIX GPIO smoke test", cmd_gpio_test);
#endif /* CONFIG_SHELL */
