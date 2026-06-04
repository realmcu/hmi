// Zephyr Shell test: uart:~$ posix_lcd
/* ================================================================
 * LCD 使用示例
 *
 * 功能：打开 LCD0 → 配置 320x480 → 设置窗口 → 刷屏 → 关闭
 *
 * 编译要求：需要 posix.h + posix_ioctl_lcd.h
 * ================================================================ */

#include "posix.h"
#include "posix_port.h"
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
#include <stdlib.h>
static bool s_posix_inited = false;

static int cmd_lcd_test(const struct shell *sh, size_t argc, char **argv)
{
    if (!s_posix_inited) { posix_port_init_all(); s_posix_inited = true; }
    shell_print(sh, "Testing POSIX LCD driver...");
    posix_fd_t fd = posix_open("/dev/lcd0");
    if (fd == POSIX_FD_NULL) { shell_error(sh, "Failed to open /dev/lcd0"); return -1; }
    posix_lcd_rect_t win = {0, 0, 10, 10};
    posix_ioctl(fd, POSIX_LCD_IOCTL_SET_WINDOW, &win);
    shell_print(sh, "LCD set_window(0,0,10,10) OK");
    posix_close(fd);
    shell_print(sh, "POSIX LCD test PASSED");
    return 0;
}

/* posix_lcd fill [0xRGB565]
 * lcd_write() 每次调用都重置窗口起点，因此逐行写以正确填满全屏 */
static int cmd_lcd_fill(const struct shell *sh, size_t argc, char **argv)
{
    uint16_t color = 0xF800U; /* 默认红色 */
    if (argc > 1) { color = (uint16_t)strtoul(argv[1], NULL, 0); }

    if (!s_posix_inited) { posix_port_init_all(); s_posix_inited = true; }

    posix_fd_t fd = posix_open("/dev/lcd0");
    if (fd == POSIX_FD_NULL) { shell_error(sh, "open /dev/lcd0 failed"); return -1; }

    /* 查询实际屏幕尺寸，若驱动未实现则使用 SH8601Z 默认值 */
    posix_lcd_config_t cfg = {0};
    posix_ioctl(fd, POSIX_LCD_IOCTL_GET_CONFIG, &cfg);
    int scr_w = (cfg.width  > 0) ? (int)cfg.width  : 410;
    int scr_h = (cfg.height > 0) ? (int)cfg.height : 502;

    posix_ioctl(fd, POSIX_LCD_IOCTL_DISPLAY_ON, NULL);

    /* 静态行缓冲区，支持最大宽度 410 px（SH8601Z）*/
    static uint16_t s_fill_line[410];
    int line_w = (scr_w <= 410) ? scr_w : 410;
    for (int i = 0; i < line_w; i++) { s_fill_line[i] = color; }

    /* 逐行设置 1 行高窗口并写入，避免每次 posix_write 重置到 (0,0) */
    for (int row = 0; row < scr_h; row++)
    {
        posix_lcd_rect_t win = {0, (uint16_t)row, (uint16_t)line_w, 1};
        posix_ioctl(fd, POSIX_LCD_IOCTL_SET_WINDOW, &win);
        posix_write(fd, s_fill_line, (size_t)(line_w * 2));
    }

    posix_close(fd);
    shell_print(sh, "LCD fill 0x%04X done (%dx%d)", color, line_w, scr_h);
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(posix_lcd_cmds,
                               SHELL_CMD(test, NULL, "Basic LCD test (set_window 10x10)", cmd_lcd_test),
                               SHELL_CMD_ARG(fill, NULL, "Fill screen: posix_lcd fill [0xRGB565]", cmd_lcd_fill, 0, 1),
                               SHELL_SUBCMD_SET_END
                              );
SHELL_CMD_REGISTER(posix_lcd, &posix_lcd_cmds, "POSIX LCD commands", NULL);
#endif /* CONFIG_SHELL */
