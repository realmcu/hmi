/**
 * @file    wifi_8711.c
 * @brief   SPI slave hardware mounting for the external RTL8711FA Wi-Fi chip.
 *
 * What this file does: resolve the DT nodes the snippet contributes, verify
 * they produced real devices, configure the SPI slave controller and the two
 * handshake lines, arm the W2B interrupt, and place the DMA slot buffers in the
 * non-cacheable region.  That is enough to catch a broken overlay at build time
 * (missing binding, wrong compatible, unresolvable phandle) and a broken board
 * at boot time (bus not ready, dead pad, no W2B edges) without any of the slot
 * logic.
 *
 * We are the SLAVE: the 8711 drives SCLK and CS.  See the header and
 * note/refer/spi-at-command-protocol 1.md sec.1.
 *
 * What it does NOT do: exchange a slot.  See TODO(port) at the bottom.
 */
#if defined(CONFIG_WIFI_8711)

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/linker/devicetree_regions.h>
#include <errno.h>
#include "wifi_8711.h"
#include "../protocol/ebadge_log.h"

/*----------------------------------------------------------------------------*
 *  Devicetree bindings  (all three nodes come from snippets/wifi_8711)
 *----------------------------------------------------------------------------*/
#define RES_NODE    DT_NODELABEL(wifi_8711_resources)
#define SPI_NODE    DT_NODELABEL(spi0_slave)
#define NC_NODE     DT_NODELABEL(psram1_nc)

/* Fail loudly at compile time rather than emitting a confusing
 * __device_dts_ord_NN link error if the snippet was forgotten. */
#if !DT_NODE_EXISTS(RES_NODE)
#error "wifi_8711_resources missing -- build with -S wifi_8711"
#endif
#if !DT_NODE_HAS_STATUS(SPI_NODE, okay)
#error "spi0_slave not enabled -- build with -S wifi_8711"
#endif
#if !DT_NODE_HAS_STATUS(NC_NODE, okay)
#error "psram1_nc missing -- build with -S wifi_8711"
#endif

/* B2W / READY: our output.  Raised only when RX+TX DMA are both armed. */
static const struct gpio_dt_spec b2w_gpio = GPIO_DT_SPEC_GET(RES_NODE, b2w_gpios);
/* W2B / REQUEST: the 8711 raises it to announce a slot -- interrupt input. */
static const struct gpio_dt_spec w2b_gpio = GPIO_DT_SPEC_GET(RES_NODE, w2b_gpios);

/* A slave controller has no bus peers, so there is no child DT node and no
 * SPI_DT_SPEC_GET (which would demand a frequency this bus does not have).
 * Bind the controller itself and hand-build the config.
 *
 * frequency = 0 is correct here, not an oversight: spi_rtl87x3g_configure()
 * skips both the ceiling check (`> max_frequency && !is_slave`) and the
 * baudrate divider (`if (!is_slave)`) for a slave, and the bus-off shortcut
 * that would otherwise trigger on frequency 0 is itself gated on
 * SPI_OP_MODE_MASTER.  The 8711 supplies ~18.5 MHz from its 20 MHz request.
 *
 * Mode 3 = CPOL 1 + CPHA 1 (sec.3): the driver maps SPI_MODE_CPHA to
 * SPI_CPHA_2Edge, which is what the protocol table calls "2Edge".
 */
static const struct device *const spi_dev = DEVICE_DT_GET(SPI_NODE);

static const struct spi_config spi_cfg =
{
    .frequency = 0U,
    .operation = SPI_OP_MODE_SLAVE | SPI_MODE_CPOL | SPI_MODE_CPHA |
    SPI_WORD_SET(8) | SPI_LINES_SINGLE | SPI_TRANSFER_MSB,
    .slave     = 0U,
    .cs        = {0},   /* hardware SS_N on P4_5, no GPIO CS */
};

/*----------------------------------------------------------------------------*
 *  JPEG reassembly buffer
 *
 *  In PSRAM1_NC, which app_mpu_config() already maps non-cacheable (region 2,
 *  attr 0x44), so the SPI path needs no SCB_CleanDCache_by_Addr -- the exact
 *  maintenance bug that bit the H264 render buffers once SPIC3 became
 *  cacheable.  32-byte aligned per the sec.11.1 checklist.
 *
 *  The 4096 B slot buffers used to live here too; they now belong to
 *  wifi_8711_xfer.c, which is the only code that may touch them while DMA is
 *  live.  This one stays because reassembly (sec.5.2) is a layer above the
 *  transport and is still unwritten -- it is declared now, __used, so that an
 *  unreferenced section cannot silently vanish and hide the fact that
 *  psram1_nc really did produce a linker region.
 *
 *  The region is NOLOAD and SPIC1 PSRAM only comes up inside
 *  app_system_lower_init(), so this must never carry a static initialiser.
 *----------------------------------------------------------------------------*/
#define SLOT_SECTION __attribute__((section(LINKER_DT_NODE_REGION_NAME(NC_NODE)), \
                                    aligned(32), __used__))

/* JPEG reassembly, >= the 8711's 60 KiB TCP cap (sec.5.2). */
static uint8_t jpeg_frame[WIFI_8711_JPEG_FRAME_MAX] SLOT_SECTION;

static bool s_ready;

/*----------------------------------------------------------------------------*
 *  W2B interrupt
 *
 *  Both edges matter (sec.3.1: rising announces a slot, falling releases it
 *  after the transfer), but GPIO_INT_TRIG_BOTH is NOT available on this SoC:
 *  gpio_rtl87x3g.c:577 only honours it under CONFIG_RTL87X3G_GPIO_SUPPORT_BOTH_EDGE,
 *  a symbol that exists nowhere in the Kconfig tree, so the case falls through
 *  to `default: return -ENOTSUP`.
 *
 *  Hence the polarity-flip fallback the protocol checklist itself prescribes
 *  (sec.11.1 item 3): arm one edge, and in the callback read the level and
 *  re-arm for the opposite edge.  The read-then-flip order matters -- flipping
 *  first would race a fast edge into being missed.
 *----------------------------------------------------------------------------*/
static struct gpio_callback w2b_cb_data;
static uint32_t s_w2b_rising;
static uint32_t s_w2b_falling;

static void w2b_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
    ARG_UNUSED(port);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);

    /* Logical level: GPIO_ACTIVE_HIGH from the DT is already applied. */
    int level = gpio_pin_get_dt(&w2b_gpio);

    if (level == 1)
    {
        s_w2b_rising++;
        /* Next interesting edge is the release -> arm the falling side. */
        (void)gpio_pin_interrupt_configure_dt(&w2b_gpio, GPIO_INT_EDGE_TO_INACTIVE);
    }
    else if (level == 0)
    {
        s_w2b_falling++;
        (void)gpio_pin_interrupt_configure_dt(&w2b_gpio, GPIO_INT_EDGE_TO_ACTIVE);
    }

    /* TODO(port): rising -> start the armed slot transfer; falling -> let the
     * parse task past its "wait for W2B low" gate (sec.7.2 PARSE). */
}

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

    if (!device_is_ready(spi_dev))
    {
        EBADGE_ERR("wifi8711: spi0_slave not ready");
        return -ENODEV;
    }
    if (!gpio_is_ready_dt(&b2w_gpio) || !gpio_is_ready_dt(&w2b_gpio))
    {
        EBADGE_ERR("wifi8711: handshake gpio port not ready");
        return -ENODEV;
    }

    /* B2W starts INACTIVE = low: the 8711 must not clock us before the slot
     * engine arms DMA.  Push-pull output per the sec.11.1 checklist. */
    rc = gpio_pin_configure_dt(&b2w_gpio, GPIO_OUTPUT_INACTIVE);
    if (rc != 0)
    {
        EBADGE_ERR1("wifi8711: b2w configure failed %d", rc);
        return rc;
    }

    /* W2B input; the pull-down comes from the DT flags so an unpowered 8711
     * reads a stable idle instead of floating into spurious interrupts. */
    rc = gpio_pin_configure_dt(&w2b_gpio, GPIO_INPUT);
    if (rc != 0)
    {
        EBADGE_ERR1("wifi8711: w2b configure failed %d", rc);
        return rc;
    }

    gpio_init_callback(&w2b_cb_data, w2b_isr, BIT(w2b_gpio.pin));
    rc = gpio_add_callback(w2b_gpio.port, &w2b_cb_data);
    if (rc != 0)
    {
        EBADGE_ERR1("wifi8711: w2b add_callback failed %d", rc);
        return rc;
    }

    /* Arm whichever edge comes next.  If the 8711 already has REQUEST high
     * when we boot, waiting for a rising edge that has already passed would
     * hang the link, so pick the direction from the current level. */
    rc = gpio_pin_interrupt_configure_dt(&w2b_gpio,
                                         (gpio_pin_get_dt(&w2b_gpio) == 1)
                                         ? GPIO_INT_EDGE_TO_INACTIVE
                                         : GPIO_INT_EDGE_TO_ACTIVE);
    if (rc != 0)
    {
        /* -ENOTSUP here would mean even single-edge is unavailable, which
         * would be a driver regression -- worth the explicit log. */
        EBADGE_ERR1("wifi8711: w2b irq configure failed %d", rc);
        (void)gpio_remove_callback(w2b_gpio.port, &w2b_cb_data);
        return rc;
    }

    /* Push the slave configuration into the controller now rather than at the
     * first transfer: a mismatch (mode, word size, slave-vs-master) is a
     * config-time -EINVAL from spi_rtl87x3g_configure(), and finding that out
     * during bring-up beats finding it out mid-slot.  A transceive with two
     * NULL buffer sets configures and returns without touching the bus. */
    rc = spi_transceive(spi_dev, &spi_cfg, NULL, NULL);
    if (rc != 0)
    {
        EBADGE_ERR1("wifi8711: spi slave configure failed %d", rc);
        (void)gpio_remove_callback(w2b_gpio.port, &w2b_cb_data);
        return rc;
    }

    EBADGE_LOG2("wifi8711: SLAVE on %s, mode3 8bit, b2w=P4_1(%d) low",
                spi_dev->name, (int)b2w_gpio.pin);
    EBADGE_LOG2("wifi8711: w2b=P2_7(%d) level=%d, irq armed (single-edge flip)",
                (int)w2b_gpio.pin, gpio_pin_get_dt(&w2b_gpio));
    EBADGE_LOG2("wifi8711: jpeg buf @%p %u B", (void *)jpeg_frame,
                (unsigned)sizeof(jpeg_frame));

    s_ready = true;

    /* TODO(port): what is still missing above the transport.
     *
     * The four-phase slot loop itself is DONE -- it lives in wifi_8711_xfer.c
     * and is started by wifi_8711_xfer_start().  Until someone calls that, B2W
     * stays low and the 8711 reports a READY timeout (1000 ms, sec.3.2), which
     * remains the expected symptom of "hardware mounted, transport not run".
     *
     *  1. Power/RF bring-up ordering.  main.c already drives ADC_2 high
     *     (VD33_WF_EN), so the 8711 has power; what is NOT established is how
     *     long its AT firmware needs before it starts driving W2B, or whether
     *     the EN_EXLNA / EN_EXPA RF-switch pads on this board need asserting.
     *     Both must come from the 8711 datasheet, not copied from watch/.
     *  2. The two parsers (sec.4.3 is the side-by-side header table), sitting
     *     on the wifi_8711_slot_cb_t sink.  Both CRC32s cover ONLY the payload,
     *     and the field sits at a different offset in each: JPGS at 28, ATMC at
     *     12.  Bound-check every length field before it indexes anything.
     *  3. JPEG reassembly into jpeg_frame with strict ordering (Frame Sequence
     *     and Total Size constant, Offset and Chunk Index contiguous); any
     *     violation drops the whole in-flight frame and waits for the next
     *     START (sec.5.2).
     *  4. END handling, which is the load-bearing part of the flow control:
     *     hold B2W LOW across JPU decode AND the LCDC transfer via
     *     wifi_8711_xfer_pause(), and call resume() only afterwards.  That
     *     rising edge is the sole trigger for the 8711's DONE <seq> to the
     *     phone (sec.3.3, sec.5.3).  Resuming early silently breaks end-to-end
     *     flow control instead of failing loudly.
     *  5. ATMC response matching: keep the COMMAND staged until the RESPONSE
     *     with the same Sequence arrives, and increment Sequence on every
     *     retransmit (the 8711 de-duplicates, sec.8).  The transport already
     *     guarantees slot_tx is never rewritten while TX DMA runs.
     *  6. Wire ebadge_port_softap / ebadge_port_tcp on top.  Only two commands
     *     exist in the 8711 firmware today -- AT+WLSTATE and AT+WLSTARTAP
     *     (sec.9) -- and the SoftAP self-starts at boot, so port_softap is
     *     mostly a credential read-back.  Anything more (set SSID, pick a
     *     channel) needs a firmware extension agreed with the 8711 side.
     */
    return 0;
}

bool wifi_8711_ready(void)
{
    return s_ready;
}

int wifi_8711_set_ready(bool ready)
{
    if (!s_ready)
    {
        return -ENODEV;
    }
    return gpio_pin_set_dt(&b2w_gpio, ready ? 1 : 0);
}

int wifi_8711_get_request(void)
{
    if (!s_ready)
    {
        return -ENODEV;
    }
    return gpio_pin_get_dt(&w2b_gpio);
}

int wifi_8711_get_ready(void)
{
    if (!s_ready)
    {
        return -ENODEV;
    }
    /* Reads back the output register, not the pad, which is what we want: the
     * question callers ask is "what have we promised the 8711", and a pad read
     * would answer "what is the wire doing" -- different question once the
     * 8711 or a probe is loading the line. */
    return gpio_pin_get_dt(&b2w_gpio);
}

const struct device *wifi_8711_spi_dev(void)
{
    return s_ready ? spi_dev : NULL;
}

const struct spi_config *wifi_8711_spi_cfg(void)
{
    return s_ready ? &spi_cfg : NULL;
}

void wifi_8711_get_w2b_stats(uint32_t *rising, uint32_t *falling)
{
    if (rising != NULL)
    {
        *rising = s_w2b_rising;
    }
    if (falling != NULL)
    {
        *falling = s_w2b_falling;
    }
}

#endif /* CONFIG_WIFI_8711 */
