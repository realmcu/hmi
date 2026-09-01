/**
 * @file    spi_at_master.c
 * @brief   AT-over-SPI master transport toward the RTL8711.  See the header for
 *          the role/pin/handshake rationale -- this file is the mechanism only.
 *
 * Structure, in the order the code runs:
 *   s2m_isr()        rising edge -> give the go-ahead semaphore.  Nothing else.
 *   at_thread()      block on that semaphore, run exactly one transaction
 *   do_transaction() stage-or-dummy, clock the slot, release M2S, parse, deliver
 *
 * Off to one side, on the system workqueue:
 *   ready_timeout_handler()  the 8711 never answered a request -> abandon it
 */
#if defined(CONFIG_SPI_AT_MASTER)

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/linker/devicetree_regions.h>
#include <errno.h>
#include <string.h>

#include "spi_at_master.h"
#include "../protocol/ebadge_log.h"
#include "app_dlps.h"

/*----------------------------------------------------------------------------*
 *  Devicetree
 *----------------------------------------------------------------------------*/
#define RES_NODE    DT_NODELABEL(spi_at_master_resources)
#define SPI_NODE    DT_NODELABEL(spi_at_master_device)
#define NC_NODE     DT_NODELABEL(psram1_nc)

/* Fail at compile time rather than emitting an opaque __device_dts_ord_NN link
 * error if the snippet was left off the command line. */
#if !DT_NODE_EXISTS(RES_NODE)
#error "spi_at_master_resources missing -- build with -S spi_at_master"
#endif
#if !DT_NODE_HAS_STATUS(SPI_NODE, okay)
#error "spi_at_master_device missing -- build with -S spi_at_master"
#endif
#if !DT_NODE_HAS_STATUS(NC_NODE, okay)
#error "psram1_nc missing -- build with -S spi_at_master"
#endif

/* M2S: our output.  GPIO_ACTIVE_LOW in the overlay, so logical 1 = pad LOW =
 * "prepare to receive", and GPIO_OUTPUT_INACTIVE below parks the pad HIGH. */
static const struct gpio_dt_spec m2s_gpio = GPIO_DT_SPEC_GET(RES_NODE, m2s_gpios);
/* S2M: our input, active high, rising edge = the 8711 is ready / wants to send. */
static const struct gpio_dt_spec s2m_gpio = GPIO_DT_SPEC_GET(RES_NODE, s2m_gpios);

/* A master HAS a bus peer, so unlike the slave build there is a child DT node
 * and SPI_DT_SPEC_GET is the right accessor -- it carries the frequency from
 * spi-max-frequency, which as master is load-bearing rather than decoration.
 *
 * Hardware CS on P4_5 is driven by the controller, so no GPIO CS is declared. */
static struct spi_dt_spec spi_spec = SPI_DT_SPEC_GET(
                                         SPI_NODE,
                                         SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_LINES_SINGLE | SPI_TRANSFER_MSB
#if SPI_AT_SPI_MODE_CPOL
                                         | SPI_MODE_CPOL
#endif
#if SPI_AT_SPI_MODE_CPHA
                                         | SPI_MODE_CPHA
#endif
                                         , 0);

/*----------------------------------------------------------------------------*
 *  Slot buffers
 *
 *  48 KiB of them (two slots plus the staging copy), which is why they are in
 *  PSRAM1_NC and not in the ~82 KB of DTCM.  That region is mapped non-cacheable
 *  by app_lower_init.c:app_mpu_config()
 *  (MPU region 2, attr 0x44), so GDMA needs no SCB_CleanDCache_by_Addr here --
 *  the maintenance bug that bit the H264 render buffers on a cacheable PSRAM
 *  window cannot recur on this path.  (Both PSRAM windows are now non-cacheable
 *  in this app; SPIC3 got its own region for the same reason.  This path never
 *  depended on that, and still does not -- it depends on region 2.)
 *
 *  The region is NOLOAD and SPIC1 PSRAM only comes up inside
 *  app_system_lower_init(), so nothing here may carry a static initialiser;
 *  everything is set in spi_at_master_init().
 *----------------------------------------------------------------------------*/
#define SLOT_SECTION __attribute__((section(LINKER_DT_NODE_REGION_NAME(NC_NODE)), \
                                    aligned(32), __used__))

static uint8_t slot_tx[SPI_AT_SLOT_SIZE] SLOT_SECTION;
static uint8_t slot_rx[SPI_AT_SLOT_SIZE] SLOT_SECTION;

/* The staged uplink frame.  Kept separate from slot_tx, and only copied into it
 * immediately before a transfer, so a producer can never rewrite the buffer GDMA
 * is reading -- the discipline the slave build needed a staging buffer for, kept
 * here for the same reason.
 *
 * In PSRAM alongside the slots rather than in RAM: it is a third 16 KiB buffer
 * and DTCM has only ~82 KiB for the whole application.  Nothing but the CPU
 * touches it, so the non-cacheable penalty applies to one memcpy of an AT
 * command line and is invisible. */
static uint8_t s_stage[SPI_AT_SLOT_SIZE] SLOT_SECTION;

/*----------------------------------------------------------------------------*
 *  Thread
 *
 *  Priority 5 and a 2 KiB stack, matching the slave transport this replaces:
 *  preemptible, above the GUI, below BT.  The stack figure assumes the hexdump
 *  keeps its row buffer small -- ebadge_log_hexdump() is written for exactly
 *  this constraint.
 *----------------------------------------------------------------------------*/
#define AT_STACK_SIZE     2048
#define AT_PRIORITY       5

static K_THREAD_STACK_DEFINE(at_stack, AT_STACK_SIZE);
static struct k_thread at_thread_data;

/* Given by the S2M ISR, taken by the thread.  This is the go-ahead for BOTH
 * directions -- see the header.  Counting, with an initial 0: if the 8711
 * raises S2M while we are still finishing the previous slot, the edge is
 * recorded rather than lost, which a binary semaphore would drop. */
static struct k_sem s2m_sem;
/* Signalled after a slot with a valid header has been delivered. */
static struct k_sem rx_sem;
/* Guards the staged frame against a shell thread staging while we consume. */
static struct k_mutex stage_lock;

/* Abandons a request the 8711 never answered -- see ready_timeout_handler(). */
static struct k_work_delayable ready_to_work;

/*----------------------------------------------------------------------------*
 *  State
 *----------------------------------------------------------------------------*/
static struct gpio_callback   s2m_cb_data;
static spi_at_master_rx_cb_t  s_rx_cb;
static spi_at_master_stats_t  s_stats;
static bool                   s_inited;
static bool                   s_dump_tx = true;
static bool                   s_dump_rx = true;

/* Length/validity for the staged frame declared with the slots above. */
static uint16_t s_stage_len;        /* payload length; 0 = nothing staged      */
static bool     s_stage_valid;

/*----------------------------------------------------------------------------*
 *  Sync lines
 *----------------------------------------------------------------------------*/

/* Assert = pull the pad LOW = "prepare to receive".  Logical 1 because the DT
 * declares the line GPIO_ACTIVE_LOW. */
static inline void m2s_assert(void)
{
    (void)gpio_pin_set_dt(&m2s_gpio, 1);
}

static inline void m2s_release(void)
{
    (void)gpio_pin_set_dt(&m2s_gpio, 0);
}

/* A request the 8711 never answered.
 *
 * Without this the harness wedges on the first missed go-ahead: M2S stays low
 * forever and the staged frame stays valid, so every later spi_at_master_send()
 * returns -EBUSY and the only way out is a reboot.  That reads as "the shell
 * command is broken" rather than as "the peer is not talking", which is the
 * opposite of what a bring-up harness should tell you.
 *
 * The stage_valid check is what makes this safe against a transfer that started
 * just as the timer fired: stage_into_tx() clears the flag under the same mutex
 * and cancels this work, so if we get the lock and the flag is already clear the
 * slot went out normally and there is nothing to abandon -- in particular we must
 * not touch M2S, because do_transaction() owns it until it releases it itself. */
static void ready_timeout_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    bool abandoned = false;

    k_mutex_lock(&stage_lock, K_FOREVER);
    if (s_stage_valid)
    {
        s_stage_valid = false;
        s_stage_len   = 0;
        abandoned     = true;
    }
    k_mutex_unlock(&stage_lock);

    if (abandoned)
    {
        m2s_release();
        s_stats.ready_timeout++;
        EBADGE_WARN1("atm: no s2m within %u ms -- request abandoned "
                     "(8711 powered? AT firmware running?)",
                     (unsigned)SPI_AT_READY_TIMEOUT_MS);
    }
}

static void s2m_isr(const struct device *port, struct gpio_callback *cb,
                    uint32_t pins)
{
    ARG_UNUSED(port);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);

    s_stats.s2m_edges++;
    k_sem_give(&s2m_sem);
}

/*----------------------------------------------------------------------------*
 *  One transaction
 *----------------------------------------------------------------------------*/

/* Build the outgoing slot.  Returns the payload length that went in, or 0 for a
 * downlink-only dummy.
 *
 * Only the header (and byte 0 of a dummy) is written; the tail of slot_tx keeps
 * whatever the previous frame left there.  That is intentional and matches the
 * reference: the peer bounds its read by the length field, so zeroing 16 KiB
 * per slot would cost real time to change nothing observable. */
static uint16_t stage_into_tx(void)
{
    uint16_t len = 0;

    k_mutex_lock(&stage_lock, K_FOREVER);
    if (s_stage_valid)
    {
        len = s_stage_len;
        slot_tx[0] = SPI_AT_MAGIC_0;
        slot_tx[1] = SPI_AT_MAGIC_1;
        slot_tx[2] = (uint8_t)(len & 0xFFU);
        slot_tx[3] = (uint8_t)((len >> 8) & 0xFFU);
        memcpy(&slot_tx[SPI_AT_HDR_SIZE], s_stage, len);
        s_stage_valid = false;
        s_stage_len   = 0;
        /* The go-ahead arrived, so the request is answered.  Cancelled under the
         * mutex so it cannot race the handler into abandoning a frame we have
         * already committed to sending. */
        (void)k_work_cancel_delayable(&ready_to_work);
    }
    else
    {
        slot_tx[0] = SPI_AT_DUMMY_MAGIC;
    }
    k_mutex_unlock(&stage_lock);

    return len;
}

/* Validate the received slot and hand off its payload.  Order matches the
 * reference (magic, then length, then range) and so does the failure behaviour:
 * a bad slot still releases M2S and still counts -- it is a diagnostic, not a
 * reason to stall the link. */
static void consume_rx(void)
{
    if (slot_rx[0] != SPI_AT_MAGIC_0 || slot_rx[1] != SPI_AT_MAGIC_1)
    {
        /* All-0x00 or all-0xFF here is the signature of a mode/clock mismatch
         * rather than of a peer sending garbage, which is why the first two
         * bytes are logged instead of just the count. */
        s_stats.rx_bad_magic++;
        EBADGE_WARN(EB_DIR_FROM_8711 "bad magic %02x %02x (mode/clock?)",
                    slot_rx[0], slot_rx[1]);
        return;
    }

    uint16_t len = (uint16_t)(slot_rx[2] | ((uint16_t)slot_rx[3] << 8));

    if (len > SPI_AT_PAYLOAD_MAX)
    {
        s_stats.rx_bad_len++;
        EBADGE_WARN(EB_DIR_FROM_8711 "len %u > max %u", (unsigned)len,
                    (unsigned)SPI_AT_PAYLOAD_MAX);
        return;
    }

    s_stats.rx_frames++;
    if (len == 0U)
    {
        /* A header-only slot.  Normal and frequent: it is how the 8711 completes
         * a transaction it had nothing to say in. */
        s_stats.rx_empty++;
        k_sem_give(&rx_sem);
        return;
    }

    if (s_dump_rx)
    {
        /* Threshold, not a cap: an AT reply lands under it and is dumped whole,
         * while a JPEG frame or a file body from port 9000 lands over
         * it and prints only its length.  That is the point -- bulk traffic
         * arrives as back-to-back 16 KiB slots, and dumping a head from each one
         * scrolls away the AT lines this dump exists to show. */
        ebadge_log_hexdump(EB_DIR_FROM_8711 "at", &slot_rx[SPI_AT_HDR_SIZE],
                           len, SPI_AT_DUMP_MAX);
    }

    if (s_rx_cb != NULL)
    {
        s_rx_cb(&slot_rx[SPI_AT_HDR_SIZE], len);
    }

    k_sem_give(&rx_sem);
}

static void do_transaction(void)
{
    uint16_t tx_len = stage_into_tx();

    if (tx_len > 0U && s_dump_tx)
    {
        /* Same threshold as inbound.  Everything we send is an AT command line
         * of tens of bytes, so this direction always prints whole -- it used to
         * clip at 256, which was a cost of head-truncation nobody needed. */
        ebadge_log_hexdump(EB_DIR_TO_8711 "at", &slot_tx[SPI_AT_HDR_SIZE],
                           tx_len, SPI_AT_DUMP_MAX);
    }

    /* Both buffer sets are always non-NULL.  A NULL set makes the driver
     * k_malloc() a full 16 KiB bounce buffer per transfer against a 2 KiB
     * arena (spi_rtl87x3g.c:324/335) -- it fails outright rather than
     * degrading, so this is not a micro-optimisation. */
    const struct spi_buf     tx      = { .buf = slot_tx, .len = SPI_AT_SLOT_SIZE };
    const struct spi_buf     rx      = { .buf = slot_rx, .len = SPI_AT_SLOT_SIZE };
    const struct spi_buf_set tx_set  = { .buffers = &tx, .count = 1 };
    const struct spi_buf_set rx_set  = { .buffers = &rx, .count = 1 };

    /* Hold off DLPS across the transfer.  A DLPS entry mid-transfer
     * reconfigures the SPI1 master clock underneath live DMA; on the watch that
     * lost the completion and stalled the path permanently.
     *
     * APP_DLPS_ENTER_CHECK_WIFI_8711 is a single bit with no refcount, so the
     * disable/enable must bracket one region and must not be nested -- which is
     * why it sits here around one transaction and nowhere else. */
    app_dlps_disable(APP_DLPS_ENTER_CHECK_WIFI_8711);

    int rc = spi_transceive_dt(&spi_spec, &tx_set, &rx_set);

    s_stats.last_result = rc;

    /* Release M2S before parsing, not after: the transfer is over, and the
     * reference releases at the same point (its NEXT: label).  Parsing first
     * would extend the request window across the hexdump for no reason. */
    m2s_release();
    app_dlps_enable(APP_DLPS_ENTER_CHECK_WIFI_8711);

    if (rc != 0)
    {
        s_stats.xfer_err++;
        EBADGE_ERR1("atm: transceive failed %d", rc);
        return;
    }

    s_stats.slots++;
    if (tx_len > 0U)
    {
        s_stats.tx_frames++;
    }

    consume_rx();
}

static void at_thread_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (true)
    {
        /* The S2M rise is the go-ahead for BOTH directions -- M2S-low is only a
         * request and never starts a transfer on its own.  Blocking here for an
         * uplink too is what keeps us in step with a peer that has to arm its
         * own DMA first. */
        if (k_sem_take(&s2m_sem, K_FOREVER) != 0)
        {
            continue;
        }
        do_transaction();
    }
}

/*----------------------------------------------------------------------------*
 *  Public API
 *----------------------------------------------------------------------------*/
int spi_at_master_init(void)
{
    int rc;

    if (s_inited)
    {
        return 0;
    }

    if (!spi_is_ready_dt(&spi_spec))
    {
        EBADGE_ERR("atm: spi bus not ready");
        return -ENODEV;
    }
    if (!gpio_is_ready_dt(&m2s_gpio) || !gpio_is_ready_dt(&s2m_gpio))
    {
        EBADGE_ERR("atm: sync gpio port not ready");
        return -ENODEV;
    }

    /* PSRAM1_NC is NOLOAD, so these have whatever the last boot left behind. */
    memset(slot_tx, 0, sizeof(slot_tx));
    memset(slot_rx, 0, sizeof(slot_rx));
    s_stage_valid = false;
    s_stage_len   = 0;
    memset(&s_stats, 0, sizeof(s_stats));

    k_sem_init(&s2m_sem, 0, K_SEM_MAX_LIMIT);
    k_sem_init(&rx_sem, 0, K_SEM_MAX_LIMIT);
    k_mutex_init(&stage_lock);
    k_work_init_delayable(&ready_to_work, ready_timeout_handler);

    /* INACTIVE = released.  With GPIO_ACTIVE_LOW in the DT that parks the pad
     * HIGH, which is the specified idle level. */
    rc = gpio_pin_configure_dt(&m2s_gpio, GPIO_OUTPUT_INACTIVE);
    if (rc != 0)
    {
        EBADGE_ERR1("atm: m2s configure failed %d", rc);
        return rc;
    }

    rc = gpio_pin_configure_dt(&s2m_gpio, GPIO_INPUT);
    if (rc != 0)
    {
        EBADGE_ERR1("atm: s2m configure failed %d", rc);
        return rc;
    }

    gpio_init_callback(&s2m_cb_data, s2m_isr, BIT(s2m_gpio.pin));
    rc = gpio_add_callback(s2m_gpio.port, &s2m_cb_data);
    if (rc != 0)
    {
        EBADGE_ERR1("atm: s2m add_callback failed %d", rc);
        return rc;
    }

    /* Only the rising edge matters as master: it is the go-ahead.  The falling
     * edge carries no information we act on, so unlike the slave build there is
     * no need for the single-edge polarity-flip dance (GPIO_INT_TRIG_BOTH is
     * -ENOTSUP on this SoC -- gpio_rtl87x3g.c:577 gates it on a Kconfig symbol
     * that does not exist). */
    rc = gpio_pin_interrupt_configure_dt(&s2m_gpio, GPIO_INT_EDGE_TO_ACTIVE);
    if (rc != 0)
    {
        EBADGE_ERR1("atm: s2m irq configure failed %d", rc);
        (void)gpio_remove_callback(s2m_gpio.port, &s2m_cb_data);
        return rc;
    }

    /* Push the configuration into the controller now rather than at the first
     * transfer.  As master the frequency is checked here (spi_rtl87x3g.c:506
     * rejects > max_bus_freq), so a 25 MHz request on a 20 MHz node fails at
     * bring-up instead of mid-command.  Two NULL buffer sets configure and
     * return without touching the bus -- the one place NULL is correct. */
    rc = spi_transceive_dt(&spi_spec, NULL, NULL);
    if (rc != 0)
    {
        EBADGE_ERR1("atm: spi master configure failed %d "
                    "(25MHz needs spi1_hs, not spi0/1/2)", rc);
        (void)gpio_remove_callback(s2m_gpio.port, &s2m_cb_data);
        return rc;
    }

    s_inited = true;

    k_thread_create(&at_thread_data, at_stack, K_THREAD_STACK_SIZEOF(at_stack),
                    at_thread_entry, NULL, NULL, NULL,
                    AT_PRIORITY, 0, K_NO_WAIT);
    (void)k_thread_name_set(&at_thread_data, "spi-at-m");

    EBADGE_LOG("atm: MASTER on %s @%u Hz mode%u%u, slot %u B",
               spi_spec.bus->name, (unsigned)spi_spec.config.frequency,
               (unsigned)SPI_AT_SPI_MODE_CPOL, (unsigned)SPI_AT_SPI_MODE_CPHA,
               (unsigned)SPI_AT_SLOT_SIZE);
    EBADGE_LOG("atm: m2s=out(P2_7) idle HIGH, s2m=in(P4_1) idle LOW level=%d",
               gpio_pin_get_dt(&s2m_gpio));

    /* If the 8711 booted first, S2M is already high and its rising edge is gone
     * for good.  Waiting for an edge that has already passed would deadlock the
     * link on every cold boot where we are the slower of the two, so synthesise
     * the go-ahead we missed.  The worst case is one wasted dummy slot. */
    if (gpio_pin_get_dt(&s2m_gpio) == 1)
    {
        EBADGE_LOG("atm: s2m already high at init -- synthesising go-ahead");
        k_sem_give(&s2m_sem);
    }

    return 0;
}

bool spi_at_master_ready(void)
{
    return s_inited;
}

int spi_at_master_send(const uint8_t *payload, uint16_t len)
{
    if (!s_inited)
    {
        return -ENODEV;
    }
    if (payload == NULL || len == 0U)
    {
        return -EINVAL;
    }
    if (len > SPI_AT_PAYLOAD_MAX)
    {
        return -EMSGSIZE;
    }

    k_mutex_lock(&stage_lock, K_FOREVER);
    if (s_stage_valid)
    {
        k_mutex_unlock(&stage_lock);
        return -EBUSY;
    }
    memcpy(s_stage, payload, len);
    s_stage_len   = len;
    s_stage_valid = true;
    k_mutex_unlock(&stage_lock);

    /* Request the slot.  The transfer itself waits for the 8711 to answer with
     * S2M; this call does not block on that.  The timer is what bounds the wait:
     * a request nobody answers is abandoned rather than left latched forever. */
    m2s_assert();
    (void)k_work_reschedule(&ready_to_work, K_MSEC(SPI_AT_READY_TIMEOUT_MS));
    return 0;
}

int spi_at_master_send_text(const char *text)
{
    if (text == NULL)
    {
        return -EINVAL;
    }

    size_t len = strlen(text);

    if (len == 0U)
    {
        return -EINVAL;
    }
    if (len + 2U > SPI_AT_PAYLOAD_MAX)
    {
        return -EMSGSIZE;
    }

    /* The 8711's line parser terminates on CRLF.  A command sent without one is
     * accepted here and then waits forever at the far end, which reads as a dead
     * link rather than as a malformed command -- so append rather than reject. */
    bool has_crlf = (len >= 2U) && (text[len - 2U] == '\r') && (text[len - 1U] == '\n');

    uint8_t buf[SPI_AT_TEXT_MAX + 4U];

    if (len + 2U > sizeof(buf))
    {
        /* Long commands are legal on the wire but have no bring-up use, and this
         * buffer sits on the CALLER's stack -- normally the shell thread.  Sizing
         * it for a 16 KiB payload would push the limit onto whichever thread
         * happens to call in, so the cap lives here instead. */
        return -EMSGSIZE;
    }

    memcpy(buf, text, len);
    if (!has_crlf)
    {
        buf[len++] = '\r';
        buf[len++] = '\n';
    }

    return spi_at_master_send(buf, (uint16_t)len);
}

void spi_at_master_set_rx_cb(spi_at_master_rx_cb_t cb)
{
    s_rx_cb = cb;
}

int spi_at_master_wait_rx(uint32_t timeout_ms)
{
    if (!s_inited)
    {
        return -ENODEV;
    }
    if (k_sem_take(&rx_sem, K_MSEC(timeout_ms)) != 0)
    {
        return -EAGAIN;
    }
    return 0;
}

void spi_at_master_set_dump(bool tx_on, bool rx_on)
{
    s_dump_tx = tx_on;
    s_dump_rx = rx_on;
}

void spi_at_master_get_dump(bool *tx_on, bool *rx_on)
{
    if (tx_on != NULL) { *tx_on = s_dump_tx; }
    if (rx_on != NULL) { *rx_on = s_dump_rx; }
}

void spi_at_master_get_stats(spi_at_master_stats_t *out)
{
    if (out != NULL)
    {
        *out = s_stats;
    }
}

void spi_at_master_reset_stats(void)
{
    memset(&s_stats, 0, sizeof(s_stats));
}

int spi_at_master_get_m2s(void)
{
    return s_inited ? gpio_pin_get_dt(&m2s_gpio) : -ENODEV;
}

int spi_at_master_get_s2m(void)
{
    return s_inited ? gpio_pin_get_dt(&s2m_gpio) : -ENODEV;
}

#endif /* CONFIG_SPI_AT_MASTER */
