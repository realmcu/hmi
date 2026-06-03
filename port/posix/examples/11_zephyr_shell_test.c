/*
 * posix-device Zephyr Shell test
 *
 * Usage:
 *   uart:~$ posix list       - list all known device paths
 *   uart:~$ posix info       - show posix-device info
 *   uart:~$ posix lcd        - test LCD driver
 *   uart:~$ posix touch      - read one touch point
 *   uart:~$ posix gpio r 0 12  - read GPIO0 pin12
 *   uart:~$ posix gpio w 0 12 1 - write 1 to GPIO0 pin12
 */
#include "posix_port.h"
#include "ioctls/posix_ioctl_lcd.h"
#include "ioctls/posix_ioctl_touch.h"
#include "ioctls/posix_ioctl_gpio.h"
#include <zephyr/shell/shell.h>
#include <stdlib.h>
#include <stdio.h>

static bool s_inited = false;

static void ensure_init(void)
{
    if (!s_inited) { posix_port_init_all(); s_inited = true; }
}

/* posix list */
static int cmd_posix_list(const struct shell *sh, size_t argc, char **argv)
{
    ensure_init();
    const char *paths[] = {"/dev/lcd0", "/dev/touch0", "/dev/gsensor0",
                           "/dev/gpio0", "/dev/gpio1", "/dev/uart0", "/dev/spi0"
                          };
    for (int i = 0; i < 7; i++)
    {
        posix_fd_t fd = posix_open(paths[i]);
        if (fd != POSIX_FD_NULL)
        {
            shell_print(sh, "  [OK] %s", paths[i]);
            posix_close(fd);
        }
        else
        {
            shell_print(sh, "  [--] %s (not registered)", paths[i]);
        }
    }
    return 0;
}

/* posix info */
static int cmd_posix_info(const struct shell *sh, size_t argc, char **argv)
{
    shell_print(sh, "posix-device abstraction layer");
    shell_print(sh, "  API: open/close/read/write/ioctl");
    shell_print(sh, "  Platform: RTL8773G + Zephyr RTOS");
    shell_print(sh, "  Devices: LCD/Touch/GSensor/GPIO/UART/SPI");
    return 0;
}

/* posix lcd */
static int cmd_posix_lcd(const struct shell *sh, size_t argc, char **argv)
{
    ensure_init();
    posix_fd_t fd = posix_open("/dev/lcd0");
    if (fd == POSIX_FD_NULL) { shell_error(sh, "open /dev/lcd0 failed"); return -1; }
    posix_lcd_window_t win = {0, 0, 10, 10};
    int r = posix_ioctl(fd, POSIX_LCD_IOCTL_SET_WINDOW, &win);
    shell_print(sh, "lcd set_window(0,0,10,10): %s", r == 0 ? "OK" : "FAIL");
    posix_close(fd);
    return 0;
}

/* posix touch */
static int cmd_posix_touch(const struct shell *sh, size_t argc, char **argv)
{
    ensure_init();
    posix_fd_t fd = posix_open("/dev/touch0");
    if (fd == POSIX_FD_NULL) { shell_error(sh, "open /dev/touch0 failed"); return -1; }
    posix_touch_data_t d;
    int r = posix_read(fd, &d, sizeof(d));
    if (r >= 0) { shell_print(sh, "touch: x=%d y=%d pressed=%d ts=%u", d.x, d.y, d.pressed, d.timestamp_ms); }
    else { shell_error(sh, "read failed: %d", r); }
    posix_close(fd);
    return 0;
}

/* posix gpio r/w <port> <pin> [val] */
static int cmd_posix_gpio(const struct shell *sh, size_t argc, char **argv)
{
    ensure_init();
    if (argc < 4) { shell_error(sh, "usage: posix gpio <r|w> <port> <pin> [val]"); return -1; }
    char op = argv[1][0];
    int port = atoi(argv[2]);
    int pin  = atoi(argv[3]);
    char path[32];
    snprintf(path, sizeof(path), "/dev/gpio%d/p%d", port, pin);
    posix_fd_t fd = posix_open(path);
    if (fd == POSIX_FD_NULL) { shell_error(sh, "open %s failed", path); return -1; }
    if (op == 'r')
    {
        posix_gpio_config_t cfg = {POSIX_GPIO_DIR_INPUT, POSIX_GPIO_PULL_NONE, 0};
        posix_ioctl(fd, POSIX_GPIO_IOCTL_SET_DIR, &cfg);
        int val;
        posix_read(fd, &val, sizeof(val));
        shell_print(sh, "gpio%d/p%d = %d", port, pin, val);
    }
    else if (op == 'w')
    {
        if (argc < 5) { shell_error(sh, "usage: posix gpio w <port> <pin> <val>"); posix_close(fd); return -1; }
        int val = atoi(argv[4]);
        posix_gpio_config_t cfg = {POSIX_GPIO_DIR_OUTPUT, POSIX_GPIO_PULL_NONE, val};
        posix_ioctl(fd, POSIX_GPIO_IOCTL_SET_DIR, &cfg);
        posix_write(fd, &val, sizeof(val));
        shell_print(sh, "gpio%d/p%d <= %d", port, pin, val);
    }
    posix_close(fd);
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(posix_cmds,
                               SHELL_CMD(list,   NULL, "List registered posix devices",    cmd_posix_list),
                               SHELL_CMD(info,   NULL, "Show posix-device info",           cmd_posix_info),
                               SHELL_CMD(lcd,    NULL, "Test LCD driver",                  cmd_posix_lcd),
                               SHELL_CMD(touch,  NULL, "Read touch point",                 cmd_posix_touch),
                               SHELL_CMD_ARG(gpio, NULL, "GPIO r/w: gpio <r|w> <port> <pin> [val]", cmd_posix_gpio, 3, 2),
                               SHELL_SUBCMD_SET_END
                              );
SHELL_CMD_REGISTER(posix, &posix_cmds, "POSIX device abstraction test commands", NULL);
