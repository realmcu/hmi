/*
 * app_display.c
 *
 * Display bring-up. Every LCD_DET interrupt asks the GUI thread to run
 * the panel initialization flow.
 */

#include "app_display.h"

#include "posix.h"
#include "posix_port.h"
#include "ioctls/posix_ioctl_gpio.h"
#include "trace.h"

#include "gui_server.h"
#include "gui_message.h"

#define LCD_DET_PATH "/dev/gpio1/p9"  /* P4_2 / GPIOB9 */

static posix_fd_t s_lcd_det_fd = POSIX_FD_NULL;

static void lcd_det_isr(void *arg)
{
    (void)arg;

    DBG_DIRECT("lcd_det: irq");

    gui_msg_t msg = { .event = GUI_EVENT_DISPLAY_INIT };
    (void)gui_send_msg_to_server(&msg);
}

static void lcd_det_init(void)
{
    (void)posix_port_init_all();

    s_lcd_det_fd = posix_open(LCD_DET_PATH);
    if (s_lcd_det_fd == POSIX_FD_NULL)
    {
        DBG_DIRECT("lcd_det: open failed");
        return;
    }

    posix_gpio_config_t cfg = {
        .pin = 0,
        .direction = POSIX_GPIO_DIR_INPUT,
        .pull = POSIX_GPIO_PULL_UP,
    };
    if (posix_ioctl(s_lcd_det_fd, POSIX_GPIO_IOCTL_SET_DIR, &cfg) != POSIX_OK)
    {
        DBG_DIRECT("lcd_det: gpio config failed");
        return;
    }

    posix_gpio_irq_t irq = {
        .pin = 0,
        .trigger = POSIX_GPIO_INT_FALLING,
        .callback = lcd_det_isr,
        .arg = NULL,
    };
    if (posix_ioctl(s_lcd_det_fd, POSIX_GPIO_IOCTL_SET_IRQ, &irq) != POSIX_OK)
    {
        DBG_DIRECT("lcd_det: irq setup failed");
        return;
    }

    if (posix_ioctl(s_lcd_det_fd, POSIX_GPIO_IOCTL_ENABLE_IRQ, NULL) != POSIX_OK)
    {
        DBG_DIRECT("lcd_det: enable irq failed");
    }
}

static int app_display_init(void)
{
    gui_server_init();
    gui_set_keep_active_time(0xFFFFFFFFu);
    lcd_det_init();
    return 0;
}

const app_module_t app_display_module =
{
    .name = "display",
    .init = app_display_init,
};
