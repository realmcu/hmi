/**
 * @file    wifi_8711_shell.c
 * @brief   `wifi8711` shell group -- bring-up probes for the SPI mounting.
 *
 * Deliberately thin: these commands exercise only what the skeleton owns
 * (devicetree resolution + handshake pin control).  Once the AT engine lands,
 * the interesting commands (wlconn, socket, throughput) come with it.
 */
#if defined(CONFIG_WIFI_8711) && defined(CONFIG_WIFI_8711_TEST)

#include <zephyr/shell/shell.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <stdlib.h>
#include "wifi_8711.h"

#define RES_NODE  DT_NODELABEL(wifi_8711_resources)
#define SPI_NODE  DT_NODELABEL(wifi_8711_device)

static const struct gpio_dt_spec m2s_gpio = GPIO_DT_SPEC_GET(RES_NODE, m2s_gpios);
static const struct gpio_dt_spec s2m_gpio = GPIO_DT_SPEC_GET(RES_NODE, s2m_gpios);

static int cmd_init(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    int rc = wifi_8711_init();
    shell_print(sh, "wifi_8711_init() = %d (%s)", rc,
                (rc == 0) ? "ok" : "failed");
    return rc;
}

/* Dumps what the overlay actually produced -- the fastest way to tell a
 * silently-wrong pin assignment from a wiring problem. */
static int cmd_info(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_print(sh, "ready       : %s", wifi_8711_ready() ? "yes" : "no");
    shell_print(sh, "spi freq    : %u Hz",
                (unsigned)DT_PROP(SPI_NODE, spi_max_frequency));
    shell_print(sh, "m2s         : port=%s pin=%d",
                m2s_gpio.port->name, (int)m2s_gpio.pin);
    shell_print(sh, "s2m         : port=%s pin=%d",
                s2m_gpio.port->name, (int)s2m_gpio.pin);

    if (wifi_8711_ready())
    {
        /* Logical level: the ACTIVE_LOW on m2s is already applied here. */
        shell_print(sh, "s2m level   : %d (logical)",
                    gpio_pin_get_dt(&s2m_gpio));
    }
    return 0;
}

/* Manual M2S toggle: with a scope on P4_1 this separates "our pin config is
 * wrong" from "the chip is not answering". */
static int cmd_m2s(const struct shell *sh, size_t argc, char **argv)
{
    if (argc != 2)
    {
        shell_error(sh, "usage: wifi8711 m2s <0|1>");
        return -EINVAL;
    }
    if (!wifi_8711_ready())
    {
        shell_error(sh, "run `wifi8711 init` first");
        return -ENODEV;
    }

    int val = atoi(argv[1]);
    int rc  = gpio_pin_set_dt(&m2s_gpio, val);
    shell_print(sh, "m2s <- %d (logical), rc=%d", val, rc);
    return rc;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_wifi8711,
                               SHELL_CMD(init, NULL, "probe DT resources and configure handshake pins",
                                         cmd_init),
                               SHELL_CMD(info, NULL, "show resolved SPI / handshake wiring", cmd_info),
                               SHELL_CMD(m2s,  NULL, "drive the M2S line: m2s <0|1>",         cmd_m2s),
                               SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(wifi8711, &sub_wifi8711, "External 8711 Wi-Fi (AT-over-SPI)", NULL);

#endif /* CONFIG_WIFI_8711 && CONFIG_WIFI_8711_TEST */
