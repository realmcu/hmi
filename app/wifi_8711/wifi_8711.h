/**
 * @file    wifi_8711.h
 * @brief   External 8711 Wi-Fi chip over SPI (AT-over-SPI) -- skeleton.
 *
 * SCOPE: this is the mounting skeleton only.  It proves the devicetree and
 * Kconfig plumbing resolves (SPI controller + bus peer + M2S/S2M handshake
 * GPIOs + non-cacheable DMA region) and gives the eBadge port layer something
 * to call.  The AT engine, the SPI DMA transfer path and the socket flows are
 * NOT here -- they get ported from
 * applications/watch/src/module/wifi_8711/ (~3700 lines).
 *
 * Architecture, for whoever lands the port:
 *
 *   eBadge protocol (l2_task)
 *        |  port_softap / port_tcp
 *   AT command engine          <- app_spi_atcmd.c    (to be ported)
 *        |  [AT][len][data][crc32][pad] frames
 *   SPI DMA master + M2S/S2M   <- app_spi_master_zephyr.c (to be ported)
 *        |  spi1_hs, 4 MHz, hardware CS
 *   8711 chip (owns the TCP/IP stack in its own firmware)
 *
 * Note the last line: there is no lwIP and no Zephyr net_if on this SoC.
 * Sockets are AT commands (AT+SKTCFG / AT+SKTSERVER / AT+SKTSENDRAW / ...),
 * so ebadge_port_tcp becomes an AT adapter rather than a socket wrapper.
 */
#ifndef _WIFI_8711_H_
#define _WIFI_8711_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Probe and latch the devicetree resources for the 8711 link.
 *
 * Checks that the SPI bus is ready and configures the two handshake GPIOs.
 * Does NOT power the chip, send any AT command, or start a transfer -- see
 * the TODO block in wifi_8711.c for what a real init still owes.
 *
 * @retval 0        resources are present and ready
 * @retval -ENODEV  SPI controller or a GPIO port is not ready
 * @retval <0       gpio configuration failed (errno from the gpio driver)
 */
int wifi_8711_init(void);

/** True once wifi_8711_init() has succeeded. */
bool wifi_8711_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* _WIFI_8711_H_ */
