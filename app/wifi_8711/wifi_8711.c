/**
 * @file    wifi_8711.c
 * @brief   Devicetree mounting skeleton for the external 8711 Wi-Fi chip.
 *
 * What this file does: resolve the DT nodes the snippet contributes, verify
 * they produced real devices, configure the two handshake lines, and log the
 * resulting wiring.  That is enough to catch a broken overlay at build time
 * (missing binding, wrong compatible, unresolvable phandle) and a broken
 * board at boot time (bus not ready) without any of the transfer logic.
 *
 * What it does NOT do: talk to the chip.  See TODO(port) at the bottom.
 */
#if defined(CONFIG_WIFI_8711)

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <errno.h>
#include "wifi_8711.h"
#include "../protocol/ebadge_log.h"

/*----------------------------------------------------------------------------*
 *  Devicetree bindings  (all three nodes come from snippets/wifi_8711)
 *----------------------------------------------------------------------------*/
#define RES_NODE  DT_NODELABEL(wifi_8711_resources)
#define SPI_NODE  DT_NODELABEL(wifi_8711_device)

/* Fail loudly at compile time rather than emitting a confusing
 * __device_dts_ord_NN link error if the snippet was forgotten. */
#if !DT_NODE_EXISTS(RES_NODE)
#error "wifi_8711_resources missing -- build with -S wifi_8711"
#endif
#if !DT_NODE_EXISTS(SPI_NODE)
#error "wifi_8711_device missing -- build with -S wifi_8711"
#endif

/* M2S: we drive it to tell the 8711 a frame is coming.  Active low per the
 * board wiring, so gpio_pin_set_dt(1) pulls the pad down. */
static const struct gpio_dt_spec m2s_gpio = GPIO_DT_SPEC_GET(RES_NODE, m2s_gpios);
/* S2M: the 8711 raises it when it has a frame for us -- interrupt input. */
static const struct gpio_dt_spec s2m_gpio = GPIO_DT_SPEC_GET(RES_NODE, s2m_gpios);

/* SPI_DT_SPEC_GET pulls frequency + CS straight out of the DT child node, so
 * the 4 MHz and the hardware SS_N live in the overlay, not here. */
static const struct spi_dt_spec spi_spec = SPI_DT_SPEC_GET(
                                               SPI_NODE, SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_LINES_SINGLE, 0);

static bool s_ready;

/*----------------------------------------------------------------------------*
 *  Init
 *----------------------------------------------------------------------------*/
int wifi_8711_init(void)
{
    int rc;

    if (s_ready)
    {
        return 0;
    }

    /* spi_is_ready_dt() checks the controller AND the CS gpio if one is
     * declared; with hardware SS_N there is no CS gpio, so this is purely the
     * controller.  A failure here almost always means the overlay's
     * status="okay" did not take effect. */
    if (!spi_is_ready_dt(&spi_spec))
    {
        EBADGE_ERR("wifi8711: spi bus not ready");
        return -ENODEV;
    }
    if (!gpio_is_ready_dt(&m2s_gpio) || !gpio_is_ready_dt(&s2m_gpio))
    {
        EBADGE_ERR("wifi8711: handshake gpio port not ready");
        return -ENODEV;
    }

    /* M2S idles INACTIVE: the flag is logical, so the driver applies the
     * GPIO_ACTIVE_LOW from the DT and parks the pad high. */
    rc = gpio_pin_configure_dt(&m2s_gpio, GPIO_OUTPUT_INACTIVE);
    if (rc != 0)
    {
        EBADGE_ERR1("wifi8711: m2s configure failed %d", rc);
        return rc;
    }
    rc = gpio_pin_configure_dt(&s2m_gpio, GPIO_INPUT);
    if (rc != 0)
    {
        EBADGE_ERR1("wifi8711: s2m configure failed %d", rc);
        return rc;
    }

    EBADGE_LOG2("wifi8711: spi ready @%u Hz, cs-hw, m2s=%d",
                (unsigned)spi_spec.config.frequency, (int)m2s_gpio.pin);
    EBADGE_LOG1("wifi8711: s2m=%d (irq not armed yet)", (int)s2m_gpio.pin);

    s_ready = true;

    /* TODO(port): everything that actually talks to the chip.  Port from
     * applications/watch/src/module/wifi_8711/ in this order:
     *
     *  1. Power/RF bring-up.  NOT in devicetree -- applications/eBadge does it
     *     with raw Pad_Config/Pinmux_Config (WIFI_EN low 200ms, high, wait 2s
     *     for the AT firmware; then EN_EXLNA / EN_EXPA on the RF switch pads).
     *     The 8711's own timing must come from its datasheet, not copied.
     *  2. app_spi_master_zephyr.c: TX ring + ping-pong RX in SECTION_PSRAM1_NC
     *     (the psram1_nc node exists for exactly this), async DMA transfers
     *     serialised by an idle flag, S2M interrupt -> gpio callback -> post
     *     to a task, M2S asserted around each transfer.  Keep the ring_buf
     *     control struct in internal RAM: no LDREX/STREX on PSRAM.
     *  3. app_spi_atcmd.c: [AT][len(2)][data][crc32][pad] framing
     *     (SPI_FRAME_CRC_EN 0 -- the ameba slave appends no checksum), the
     *     command/response table with per-command timeouts, and the two-phase
     *     AT+SKTSENDRAW (send cmd, wait ">>>", push raw, wait "OK").
     *  4. Decouple the watch-only includes while porting: app_cmd.h,
     *     app_dlps.h, trace.h, rtk_errno.h.  psram_section.h has to be copied
     *     in (or the SECTION_PSRAM1_NC macro inlined) since it lives under
     *     applications/watch/src/app/.
     *  5. Wire ebadge_port_tcp on top as an AT adapter: listen ->
     *     AT+SKTCFG + AT+SKTSERVER, on_data <- unsolicited RX frames (batch to
     *     >=4KB per the port_tcp contract; 16KB SPI frames satisfy it),
     *     send -> AT+SKTSENDRAW, close -> AT+SKTDEL.
     *  6. ebadge_port_softap is the open question: only AT+WLCONN (station)
     *     was found in the watch module, no AP-mode command.  eBadge V1.2
     *     needs the device to HOST the AP.  Confirm against the 8711 AT
     *     firmware docs before assuming port_softap is implementable.
     */
    return 0;
}

bool wifi_8711_ready(void)
{
    return s_ready;
}

#endif /* CONFIG_WIFI_8711 */
