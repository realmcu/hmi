// Zephyr Shell test: uart:~$ posix_lcd
/* ================================================================
 * LCD 使用示例
 *
 * 功能：打开 LCD0 → 配置 320x480 → 设置窗口 → 刷屏 → 关闭
 *
 * 编译要求：需要 posix.h + posix_ioctl_lcd.h
 * ================================================================ */

#include "posix.h"
#include "ioctls/posix_ioctl_lcd.h"

void example_lcd(void)
{
    /* === 1. 打开 === */
    posix_fd_t lcd = posix_open("/dev/lcd0");
    if (!lcd) { return; }

    /* === 2. 配置 === */
    posix_lcd_config_t cfg =
    {
        .width = 320, .height = 480,
        .bpp = 16, .interface = POSIX_LCD_IF_SPI,
    };
    posix_ioctl(lcd, POSIX_LCD_IOCTL_SET_CONFIG, &cfg);

    /* === 3. 打开显示 + 背光 === */
    posix_ioctl(lcd, POSIX_LCD_IOCTL_DISPLAY_ON, NULL);
    posix_ioctl(lcd, POSIX_LCD_IOCTL_SET_BRIGHTNESS, &(int) {100});

    /* === 4. 设置窗口（全屏） === */
    posix_ioctl(lcd, POSIX_LCD_IOCTL_SET_WINDOW,
    &(posix_lcd_rect_t) { .x = 0, .y = 0, .w = 320, .h = 480 });

    /* === 5. 刷屏：发像素数据 === */
    uint16_t framebuffer[320 * 480];
    /* fill framebuffer with color... */
    posix_write(lcd, framebuffer, sizeof(framebuffer));

    /* === 6. 关显示 === */
    posix_ioctl(lcd, POSIX_LCD_IOCTL_DISPLAY_OFF, NULL);

    /* === 7. 关闭 === */
    posix_close(lcd);
}

#ifdef CONFIG_SHELL
#include <zephyr/shell/shell.h>
static bool s_posix_inited = false;

static int cmd_lcd_test(const struct shell *sh, size_t argc, char **argv)
{
    if (!s_posix_inited)
    {
        posix_port_init_all();
        s_posix_inited = true;
    }
    shell_print(sh, "Testing POSIX LCD driver...");
    posix_fd_t fd = posix_open("/dev/lcd0");
    if (fd == POSIX_FD_NULL)
    {
        shell_error(sh, "Failed to open /dev/lcd0");
        return -1;
    }
    posix_lcd_window_t win = {0, 0, 10, 10};
    posix_ioctl(fd, POSIX_LCD_IOCTL_SET_WINDOW, &win);
    shell_print(sh, "LCD window set OK");
    posix_close(fd);
    shell_print(sh, "POSIX LCD test PASSED");
    return 0;
}
SHELL_CMD_REGISTER(posix_lcd, NULL, "Test POSIX LCD: posix_lcd test", cmd_lcd_test);
#endif /* CONFIG_SHELL */
