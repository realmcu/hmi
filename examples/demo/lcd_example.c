// Zephyr Shell test: uart:~$ posix_lcd
/* ================================================================
 * LCD usage example
 *
 * Key conventions (see the top of posix_ioctl_lcd.h for details):
 *   1. The display resolution is reported by the driver; the app gets it via GET_CONFIG, do not hardcode
 *   2. posix_write's count unit is [bytes], consistent with other posix devices;
 *      one RGB565 row is width * 2 bytes, so you can write sizeof(line_buf) directly
 *   3. posix_write internally applies the rect saved by the last SET_WINDOW,
 *      so when writing row by row you must SET_WINDOW once per row
 *
 * Build requirements: posix.h + posix_ioctl_lcd.h
 * ================================================================ */

#include "posix.h"
#include "posix_port.h"
#include "ioctls/posix_ioctl_lcd.h"

/* ================================================================
 * Textbook example: query size → turn on display → fill screen with a solid color → close
 *
 * Feed data row by row to avoid putting a full-screen width*height*2-byte buffer on the stack.
 * ================================================================ */
void example_lcd(void)
{
    posix_fd_t lcd = posix_open("/dev/lcd0");
    if (lcd == POSIX_FD_NULL) { return; }

    /* 1. Query the real size from the driver; do not assume */
    posix_lcd_config_t cfg = {0};
    if (posix_ioctl(lcd, POSIX_LCD_IOCTL_GET_CONFIG, &cfg) != POSIX_OK ||
        cfg.width == 0 || cfg.height == 0)
    {
        posix_close(lcd);
        return;
    }

    /* 2. Turn on display + backlight */
    posix_ioctl(lcd, POSIX_LCD_IOCTL_DISPLAY_ON, NULL);
    posix_ioctl(lcd, POSIX_LCD_IOCTL_SET_BRIGHTNESS, &(int) {100});

    /* 3. Fill red row by row (RGB565 = 0xF800)
     *    the static line buffer's width cap is a compile-time constant, truncated to cfg.width at runtime */
#define POSIX_LCD_MAX_LINE_PX  480
    static uint16_t line_buf[POSIX_LCD_MAX_LINE_PX];
    uint16_t line_w = (cfg.width <= POSIX_LCD_MAX_LINE_PX)
                      ? cfg.width : POSIX_LCD_MAX_LINE_PX;
    for (uint16_t i = 0; i < line_w; i++) { line_buf[i] = 0xF800; }

    /* SET_WINDOW once per row; write's count is bytes (line_w * 2) */
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
 * Screen size is reported by the driver via GET_CONFIG; the app does not guess the chip model */
static int cmd_lcd_fill(const struct shell *sh, size_t argc, char **argv)
{

    uint16_t color = 0xF800U; /* default red */
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


    /* Static line buffer; compile-time cap covers any panel width */
#define POSIX_LCD_MAX_LINE_PX  480
    static uint16_t s_fill_line[POSIX_LCD_MAX_LINE_PX];
    uint16_t line_w = (cfg.width <= POSIX_LCD_MAX_LINE_PX)
                      ? cfg.width : POSIX_LCD_MAX_LINE_PX;
    for (uint16_t i = 0; i < line_w; i++) { s_fill_line[i] = color; }

    /* Row by row: SET_WINDOW + write; write count = bytes = line_w * 2 */
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
