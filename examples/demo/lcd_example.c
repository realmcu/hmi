// Zephyr Shell test: uart:~$ posix_lcd
/* ================================================================
 * LCD 使用示例
 *
 * 关键约定（详见 posix_ioctl_lcd.h 顶部）：
 *   1. 屏幕分辨率由驱动报告，应用层通过 GET_CONFIG 拿到，不要硬编码
 *   2. posix_write 的 count 单位是【字节】，与其它 posix 设备一致；
 *      RGB565 一行字节数 = width * 2，可直接写 sizeof(line_buf)
 *   3. posix_write 内部会 apply 上一次 SET_WINDOW 保存的 rect，
 *      因此逐行写时必须每行都 SET_WINDOW 一次
 *
 * 编译要求：posix.h + posix_ioctl_lcd.h
 * ================================================================ */

#include "posix.h"
#include "posix_port.h"
#include "ioctls/posix_ioctl_lcd.h"

/* ================================================================
 * 教科书示例：查询尺寸 → 打开显示 → 全屏纯色 → 关闭
 *
 * 逐行送数据，避免栈上放 width*height*2 字节的整屏 buffer。
 * ================================================================ */
void example_lcd(void)
{
    posix_fd_t lcd = posix_open("/dev/lcd0");
    if (lcd == POSIX_FD_NULL) { return; }

    /* 1. 从驱动查真实尺寸，不假设 */
    posix_lcd_config_t cfg = {0};
    if (posix_ioctl(lcd, POSIX_LCD_IOCTL_GET_CONFIG, &cfg) != POSIX_OK ||
        cfg.width == 0 || cfg.height == 0)
    {
        posix_close(lcd);
        return;
    }

    /* 2. 打开显示 + 背光 */
    posix_ioctl(lcd, POSIX_LCD_IOCTL_DISPLAY_ON, NULL);
    posix_ioctl(lcd, POSIX_LCD_IOCTL_SET_BRIGHTNESS, &(int) {100});

    /* 3. 逐行填充红色（RGB565 = 0xF800）
     *    静态行缓冲的宽度上限用编译期常量兜底，运行期按 cfg.width 截断 */
#define POSIX_LCD_MAX_LINE_PX  480
    static uint16_t line_buf[POSIX_LCD_MAX_LINE_PX];
    uint16_t line_w = (cfg.width <= POSIX_LCD_MAX_LINE_PX)
                      ? cfg.width : POSIX_LCD_MAX_LINE_PX;
    for (uint16_t i = 0; i < line_w; i++) { line_buf[i] = 0xF800; }

    /* 每行都 SET_WINDOW 一次；write 的 count 是字节数（line_w * 2） */
    for (uint16_t row = 0; row < cfg.height; row++)
    {
        posix_lcd_rect_t win = { .x = 0, .y = row, .w = line_w, .h = 1 };
        posix_ioctl(lcd, POSIX_LCD_IOCTL_SET_WINDOW, &win);
        posix_write(lcd, line_buf, (size_t)line_w * sizeof(uint16_t));
    }

    posix_ioctl(lcd, POSIX_LCD_IOCTL_DISPLAY_OFF, NULL);
    posix_close(lcd);
}

#ifdef CONFIG_SHELL
#include <zephyr/shell/shell.h>
#include <stdlib.h>
static bool s_posix_inited = false;

static int cmd_lcd_test(const struct shell *sh, size_t argc, char **argv)
{
    (void)argc; (void)argv;
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
 * 屏幕尺寸由驱动 GET_CONFIG 报告，应用不猜芯片型号 */
static int cmd_lcd_fill(const struct shell *sh, size_t argc, char **argv)
{

    uint16_t color = 0xF800U; /* 默认红色 */
    if (argc > 1) { color = (uint16_t)strtoul(argv[1], NULL, 0); }


    if (!s_posix_inited) { posix_port_init_all(); s_posix_inited = true; }

    posix_fd_t fd = posix_open("/dev/lcd0");
    if (fd == POSIX_FD_NULL) { shell_error(sh, "open /dev/lcd0 failed"); return -1; }



    posix_lcd_config_t cfg = {0};
    if (posix_ioctl(fd, POSIX_LCD_IOCTL_GET_CONFIG, &cfg) != POSIX_OK ||
        cfg.width == 0 || cfg.height == 0)
    {
        shell_error(sh, "GET_CONFIG failed or driver reported zero size");
        posix_close(fd);
        return -1;
    }


    posix_ioctl(fd, POSIX_LCD_IOCTL_DISPLAY_ON, NULL);


    /* 静态行缓冲，编译期上限兜底任何面板宽度 */
#define POSIX_LCD_MAX_LINE_PX  480
    static uint16_t s_fill_line[POSIX_LCD_MAX_LINE_PX];
    uint16_t line_w = (cfg.width <= POSIX_LCD_MAX_LINE_PX)
                      ? cfg.width : POSIX_LCD_MAX_LINE_PX;
    for (uint16_t i = 0; i < line_w; i++) { s_fill_line[i] = color; }

    /* 逐行 SET_WINDOW + write；write count = 字节数 = line_w * 2 */
    for (uint16_t row = 0; row < cfg.height; row++)
    {

        posix_lcd_rect_t win = {0, row, line_w, 1};
        posix_ioctl(fd, POSIX_LCD_IOCTL_SET_WINDOW, &win);
        posix_write(fd, s_fill_line, (size_t)line_w * sizeof(uint16_t));

    }

    posix_close(fd);
    shell_print(sh, "LCD fill 0x%04X done (%ux%u)", color, line_w, cfg.height);

    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(posix_lcd_cmds,
                               SHELL_CMD(test, NULL, "Basic LCD test (set_window 10x10)", cmd_lcd_test),
                               SHELL_CMD_ARG(fill, NULL, "Fill screen: posix_lcd fill [0xRGB565]", cmd_lcd_fill, 0, 1),
                               SHELL_SUBCMD_SET_END
                              );
SHELL_CMD_REGISTER(posix_lcd, &posix_lcd_cmds, "POSIX LCD commands", NULL);
#endif /* CONFIG_SHELL */
