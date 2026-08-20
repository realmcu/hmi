/**
 * @file    wifi_8711_xfer.c
 * @brief   Fixed 4096-byte slot transport over the SPI slave link.
 *
 * Implements the four-phase B2W/W2B rendezvous of protocol sec.3.1 and the
 * ARM/READY/RX_DONE/PARSE state machine of sec.7.2, and nothing above it.
 *
 * ---------------------------------------------------------------------------
 * Why there is a thread here at all
 * ---------------------------------------------------------------------------
 * The obvious design -- re-arm the next slot from the SPI completion callback
 * -- does not work on this driver, and the reason is worth stating precisely
 * because the code looks like it should work:
 *
 *   spi_context_complete() invokes our callback at spi_context.h:201 and only
 *   releases ctx->lock at :206, i.e. AFTER we return.  The callback itself runs
 *   in the DMA RX ISR (spi_rtl87x3g.c:402 dma_rx_cb -> :432 -> :270 complete).
 *   So calling spi_transceive_cb() from the callback reaches
 *   spi_context_lock() -> k_sem_take(&ctx->lock, K_FOREVER) at
 *   spi_context.h:101, from an ISR, on a semaphore that is still taken.
 *
 * On top of that, spi_rtl87x3g_start_dma_transceive() calls k_malloc() whenever
 * a buffer pointer is NULL (spi_rtl87x3g.c:324, :335) -- also not ISR-safe.  We
 * always pass both buffers, so we avoid that path, but it confirms the driver
 * does not intend to be re-entered from its own completion.
 *
 * Hence the split, which is also what protocol sec.11.1 item 4 asks for:
 *   ISR    -> drop B2W (must be first, it is the 8711's cue), then k_sem_give
 *   thread -> deliver the slot, wait W2B low, re-arm, raise B2W
 *
 * ---------------------------------------------------------------------------
 * Two driver behaviours that will bite anyone reading `result`
 * ---------------------------------------------------------------------------
 * 1. In slave mode a SUCCESSFUL completion reports result == 4096, not 0.
 *    spi_context_complete() overwrites status with ctx->recv_frames for slaves
 *    (spi_context.h:194-199).  Testing `result == 0` would count every good
 *    slot as a failure.
 * 2. spi_rtl87x3g_complete() calls SPI_Cmd(spi, DISABLE) at :276 and the next
 *    arm re-enables it at :384.  Clocks arriving inside that window are lost --
 *    which is precisely what B2W being low is for, so it is safe here, but it
 *    does mean B2W must never be high while we are between slots.
 *
 * Buffers live in PSRAM1_NC, which app_mpu_config() maps non-cacheable
 * (region 2, attr 0x44), so there is deliberately no SCB_CleanDCache_by_Addr
 * on this path -- see the note in wifi_8711.c.
 */
#if defined(CONFIG_WIFI_8711)

#include <zephyr/kernel.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/linker/devicetree_regions.h>
#include <errno.h>
#include <string.h>
#include "wifi_8711.h"
#include "wifi_8711_xfer.h"
#include "spi_at_protocol.h"
#include "../protocol/ebadge_log.h"

#define NC_NODE     DT_NODELABEL(psram1_nc)
#define SLOT_SECTION __attribute__((section(LINKER_DT_NODE_REGION_NAME(NC_NODE)), \
                                    aligned(32), __used__))

/*----------------------------------------------------------------------------*
 *  DMA buffers.  32-byte aligned per the sec.11.1 item 10 checklist.
 *
 *  RX is a ping-pong pair: the thread hands one half to the sink while the
 *  other is already armed for the next slot (sec.7.2 ARM/PARSE overlap).
 *
 *  The region is NOLOAD and SPIC1 PSRAM only exists after
 *  app_system_lower_init(), so none of these may carry a static initialiser.
 *----------------------------------------------------------------------------*/
static uint8_t slot_tx[WIFI_8711_SLOT_SIZE]     SLOT_SECTION;
static uint8_t slot_rx[2][WIFI_8711_SLOT_SIZE]  SLOT_SECTION;

/* Staging copy, so a caller may replace the TX content at any time without
 * ever touching slot_tx while TX DMA is live (sec.4.2, sec.11.2). */
static uint8_t slot_tx_staging[WIFI_8711_SLOT_SIZE] SLOT_SECTION;

static K_MUTEX_DEFINE(tx_lock);
static bool s_tx_dirty;

/*----------------------------------------------------------------------------*
 *  Transport state
 *----------------------------------------------------------------------------*/
#define XFER_STACK_SIZE   2048
#define XFER_PRIORITY     5      /* preemptible; above the GUI, below BT      */

/* sec.3.2 timeouts, from the 8711's perspective -- we use the same numbers so
 * that whichever side gives up first, it does so for the same reason. */
#define W2B_LOW_TIMEOUT_MS   1000U
#define SLOT_WAIT_SPIN_US    200U   /* busy-poll window before falling back   */

static K_THREAD_STACK_DEFINE(xfer_stack, XFER_STACK_SIZE);
static struct k_thread xfer_thread;

/* Given by the completion ISR, taken by the transport thread. */
static K_SEM_DEFINE(slot_done_sem, 0, 1);
/* Given on every delivered slot, for wifi_8711_xfer_test_wait_slot(). */
static K_SEM_DEFINE(slot_arrived_sem, 0, 1);

static const struct device *s_spi_dev;
static const struct spi_config *s_spi_cfg;
static wifi_8711_slot_cb_t s_sink;

static bool     s_started;
static bool     s_paused;
static uint8_t  s_rx_idx;        /* half currently armed                     */
static int      s_last_result;   /* result reported by the last completion   */

static wifi_8711_xfer_stats_t s_stats;

/* ATMC command sequence counter (sec.8: 32-bit, skips 0). */
static uint32_t s_at_seq;

/*----------------------------------------------------------------------------*
 *  Completion callback -- DMA RX ISR context.  Keep it minimal.
 *----------------------------------------------------------------------------*/
static void slot_done_cb(const struct device *dev, int result, void *data)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(data);

    /* Drop B2W FIRST, before anything else at all.  The 8711 is waiting on
     * this edge to release W2B and finish its own slot (sec.3.1 phase 4), and
     * sec.11.1 item 4 is explicit that it must happen even if we cannot handle
     * the slot -- a full queue must never leave B2W high with no DMA armed. */
    (void)wifi_8711_set_ready(false);

    s_last_result = result;

    /* Slave success is reported as the received frame count, not 0
     * (spi_context.h:194-199).  Anything short means the 8711 clocked fewer
     * bytes than a slot, which in practice means a mode/word-size mismatch. */
    if (result == (int)WIFI_8711_SLOT_SIZE)
    {
        s_stats.slots_ok++;
    }
    else if (result >= 0)
    {
        s_stats.slots_short++;
    }
    else
    {
        s_stats.slots_err++;
    }

    k_sem_give(&slot_done_sem);
}

/*----------------------------------------------------------------------------*
 *  Arm one slot: promote any staged TX, hand both buffers to the driver,
 *  then raise B2W.  Order matters -- B2W high is a promise that DMA is live.
 *----------------------------------------------------------------------------*/
static int arm_slot(void)
{
    int rc;

    /* Promote the staged TX content now, while no transfer is in flight.
     * This is the only place slot_tx is written. */
    k_mutex_lock(&tx_lock, K_FOREVER);
    if (s_tx_dirty)
    {
        memcpy(slot_tx, slot_tx_staging, WIFI_8711_SLOT_SIZE);
        s_tx_dirty = false;
    }
    k_mutex_unlock(&tx_lock);

    /* Both buffer sets are always supplied: a NULL side would make the driver
     * k_malloc() a scratch buffer on every single slot
     * (spi_rtl87x3g.c:324/335) -- needless heap churn at slot rate. */
    const struct spi_buf tx_buf = { .buf = slot_tx,           .len = WIFI_8711_SLOT_SIZE };
    const struct spi_buf rx_buf = { .buf = slot_rx[s_rx_idx], .len = WIFI_8711_SLOT_SIZE };
    const struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };
    const struct spi_buf_set rx_set = { .buffers = &rx_buf, .count = 1 };

    rc = spi_transceive_cb(s_spi_dev, s_spi_cfg, &tx_set, &rx_set,
                           slot_done_cb, NULL);
    if (rc != 0)
    {
        s_stats.arm_fail++;
        EBADGE_ERR1("wifi8711 xfer: arm failed %d", rc);
        return rc;
    }

    s_stats.arms++;

    /* DMA is live -> B2W may go high.  Unless the layer above is holding the
     * link low across a JPU decode + LCDC transfer (sec.3.3 layer 2), in which
     * case the rising edge is its to give, not ours. */
    if (!s_paused)
    {
        rc = wifi_8711_set_ready(true);
        if (rc != 0)
        {
            EBADGE_ERR1("wifi8711 xfer: b2w raise failed %d", rc);
        }
    }
    return 0;
}

/*----------------------------------------------------------------------------*
 *  Wait for W2B to fall (sec.7.2 PARSE gate).
 *
 *  Polled rather than driven off the W2B interrupt on purpose.  The edge
 *  arrives microseconds after our B2W drop, so a short spin catches essentially
 *  every case without the cross-module ISR plumbing a semaphore handoff would
 *  need; and the slow path only runs when the link is already misbehaving.
 *  The interrupt in wifi_8711.c stays as the edge counter for diagnosis.
 *----------------------------------------------------------------------------*/
static bool wait_w2b_low(void)
{
    if (wifi_8711_get_request() == 0)
    {
        return true;
    }

    for (uint32_t spun = 0; spun < SLOT_WAIT_SPIN_US; spun += 10U)
    {
        k_busy_wait(10);
        if (wifi_8711_get_request() == 0)
        {
            return true;
        }
    }

    for (uint32_t ms = 0; ms < W2B_LOW_TIMEOUT_MS; ms++)
    {
        k_msleep(1);
        if (wifi_8711_get_request() == 0)
        {
            return true;
        }
    }

    /* sec.3.2: the 8711 gives up after 1000 ms too.  Do not force a slot
     * through -- carry on to the next ARM and let the counter show it. */
    s_stats.w2b_wait_to++;
    EBADGE_WARN("wifi8711 xfer: W2B stuck high, re-arming anyway");
    return false;
}

/* Cheap "is this slot actually data" check, so bring-up can tell a real
 * exchange from MISO/MOSI being dead and clocking in zeroes. */
static bool slot_is_nonzero(const uint8_t *p)
{
    for (size_t i = 0; i < WIFI_8711_SLOT_SIZE; i++)
    {
        if (p[i] != 0U)
        {
            return true;
        }
    }
    return false;
}

/*----------------------------------------------------------------------------*
 *  Transport thread: PARSE -> ARM, forever.
 *----------------------------------------------------------------------------*/
static void xfer_thread_fn(void *a, void *b, void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);

    while (true)
    {
        /* The 8711 idles at ~2 s per POLL (sec.4.1 item 6), so there is no
         * meaningful upper bound on how long a slot may take to arrive.
         * K_FOREVER is correct: a silent link is not an error here, it is a
         * peer that has nothing to say. */
        k_sem_take(&slot_done_sem, K_FOREVER);

        uint8_t done_idx = s_rx_idx;

        /* Flip to the other half before delivering, so the buffer we hand out
         * is not the one the next arm will overwrite. */
        s_rx_idx ^= 1U;

        if (s_last_result == (int)WIFI_8711_SLOT_SIZE)
        {
            if (slot_is_nonzero(slot_rx[done_idx]))
            {
                s_stats.rx_nonzero++;
            }

            /* Deliver before waiting on W2B: the sink may want to start work
             * immediately, and B2W is already low so the 8711 is not blocked
             * on us for anything except the next ARM. */
            if (s_sink != NULL)
            {
                s_sink(slot_rx[done_idx], WIFI_8711_SLOT_SIZE);
            }
            k_sem_give(&slot_arrived_sem);
        }

        (void)wait_w2b_low();

        if (arm_slot() != 0)
        {
            /* Back off rather than spin: a failing arm is usually the driver
             * still holding its context, which clears on its own. */
            k_msleep(10);
        }
    }
}

/*----------------------------------------------------------------------------*
 *  Public API
 *----------------------------------------------------------------------------*/
int wifi_8711_xfer_start(wifi_8711_slot_cb_t cb)
{
    int rc;

    if (s_started)
    {
        return 0;
    }
    if (!wifi_8711_ready())
    {
        EBADGE_ERR("wifi8711 xfer: call wifi_8711_init() first");
        return -ENODEV;
    }

    s_spi_dev = wifi_8711_spi_dev();
    s_spi_cfg = wifi_8711_spi_cfg();
    if (s_spi_dev == NULL || s_spi_cfg == NULL)
    {
        return -ENODEV;
    }

    s_sink   = cb;
    s_rx_idx = 0U;
    s_paused = false;
    memset(&s_stats, 0, sizeof(s_stats));

    /* Idle TX content until someone stages something (sec.4.2 allows all
     * zeroes; the 8711 simply finds no valid ATMC COMMAND on MISO). */
    memset(slot_tx, 0, sizeof(slot_tx));
    memset(slot_tx_staging, 0, sizeof(slot_tx_staging));
    s_tx_dirty = false;

    /* Arm the FIRST slot before the thread exists.  If the 8711 booted ahead
     * of us its opening W2B edge is already gone, and a transport that only
     * armed on an edge would sit there forever -- the latent bug in the
     * bt_audio_trx reference, which never pre-arms. */
    rc = arm_slot();
    if (rc != 0)
    {
        return rc;
    }

    k_thread_create(&xfer_thread, xfer_stack, K_THREAD_STACK_SIZEOF(xfer_stack),
                    xfer_thread_fn, NULL, NULL, NULL,
                    XFER_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&xfer_thread, "wifi8711_xfer");

    s_started = true;
    EBADGE_LOG2("wifi8711 xfer: started, slot=%u B, first slot armed, b2w=%d",
                (unsigned)WIFI_8711_SLOT_SIZE, wifi_8711_get_ready());
    return 0;
}

bool wifi_8711_xfer_started(void)
{
    return s_started;
}

int wifi_8711_xfer_set_tx(const uint8_t *slot)
{
    if (slot == NULL)
    {
        return -EINVAL;
    }
    if (!s_started)
    {
        return -ENODEV;
    }

    k_mutex_lock(&tx_lock, K_FOREVER);
    memcpy(slot_tx_staging, slot, WIFI_8711_SLOT_SIZE);
    s_tx_dirty = true;
    k_mutex_unlock(&tx_lock);
    return 0;
}

int wifi_8711_xfer_set_tx_idle(void)
{
    if (!s_started)
    {
        return -ENODEV;
    }

    k_mutex_lock(&tx_lock, K_FOREVER);
    memset(slot_tx_staging, 0, WIFI_8711_SLOT_SIZE);
    s_tx_dirty = true;
    k_mutex_unlock(&tx_lock);
    return 0;
}

int wifi_8711_xfer_pause(void)
{
    if (!s_started)
    {
        return -ENODEV;
    }
    s_paused = true;
    return wifi_8711_set_ready(false);
}

int wifi_8711_xfer_resume(void)
{
    if (!s_started)
    {
        return -ENODEV;
    }
    s_paused = false;
    /* This rising edge is what the 8711 turns into DONE <seq> for the phone
     * (sec.3.3, sec.5.3).  Only the display path may produce it. */
    return wifi_8711_set_ready(true);
}

int wifi_8711_xfer_set_sink(wifi_8711_slot_cb_t cb)
{
    /* Assigning a function pointer is atomic on this core, and the transport
     * thread only ever reads it, so no lock is needed.  Worst case a sink
     * installed mid-slot misses that one slot -- which is why this exists at
     * all: the alternative is a caller that arrives after the shell already
     * started the transport with a NULL sink and then never sees a byte. */
    s_sink = cb;
    return s_started ? 0 : -ENODEV;
}

void wifi_8711_xfer_get_stats(wifi_8711_xfer_stats_t *out)
{
    if (out == NULL)
    {
        return;
    }
    *out = s_stats;
    out->last_result = (uint32_t)s_last_result;
}

void wifi_8711_xfer_reset_stats(void)
{
    memset(&s_stats, 0, sizeof(s_stats));
}

size_t wifi_8711_xfer_peek_rx(uint8_t *dst, size_t len)
{
    if (dst == NULL || len == 0U)
    {
        return 0U;
    }
    if (s_stats.slots_ok == 0U && s_stats.slots_short == 0U)
    {
        return 0U;
    }
    if (len > WIFI_8711_SLOT_SIZE)
    {
        len = WIFI_8711_SLOT_SIZE;
    }
    /* s_rx_idx has already flipped, so the last completed half is the other. */
    memcpy(dst, slot_rx[s_rx_idx ^ 1U], len);
    return len;
}

/*----------------------------------------------------------------------------*
 *  Test API
 *
 *  Both builders write straight into slot_tx_staging under tx_lock rather than
 *  into a local 4096-byte scratch.  A scratch would either blow the 4 KB main
 *  stack or cost another 4 KB static in DTCM, and there is no reason for it --
 *  the staging buffer is exactly the right place and is already protected.
 *----------------------------------------------------------------------------*/
int wifi_8711_xfer_test_pattern(uint32_t seed)
{
    if (!s_started)
    {
        return -ENODEV;
    }

    k_mutex_lock(&tx_lock, K_FOREVER);

    memcpy(slot_tx_staging, "8773TEST", 8);
    spi_at_put_le32(slot_tx_staging + 8, seed);
    spi_at_put_le32(slot_tx_staging + 12, WIFI_8711_SLOT_SIZE);

    /* A ramp, not a constant: a stuck data line or a byte-order mistake stays
     * visible, whereas a constant fill looks identical however it breaks. */
    for (size_t i = 16; i < WIFI_8711_SLOT_SIZE; i++)
    {
        slot_tx_staging[i] = (uint8_t)((i + seed) & 0xFFU);
    }

    s_tx_dirty = true;
    k_mutex_unlock(&tx_lock);

    EBADGE_LOG1("wifi8711 xfer: staged test pattern seed=%u", (unsigned)seed);
    return 0;
}

int wifi_8711_xfer_test_at(const char *text, uint32_t *out_seq)
{
    uint32_t seq;
    int len;

    if (text == NULL)
    {
        return -EINVAL;
    }
    if (!s_started)
    {
        return -ENODEV;
    }
    /* sec.9: the 8711's parse buffer is 128 B and a longer payload is dropped
     * silently -- so reject it here, loudly, instead. */
    if (strlen(text) >= WIFI_8711_AT_COMMAND_MAX)
    {
        EBADGE_ERR1("wifi8711 xfer: AT payload %u B >= 128", (unsigned)strlen(text));
        return -EMSGSIZE;
    }

    /* Sequence must advance on every call including a retransmit of the same
     * text: the 8711 de-duplicates on Sequence (sec.8), so re-sending with the
     * old number gets silently swallowed and the caller waits forever. */
    seq = spi_at_next_sequence(&s_at_seq);

    k_mutex_lock(&tx_lock, K_FOREVER);
    /* Builds the whole 4096 B slot, zero-padding the tail itself. */
    len = spi_at_build_packet(slot_tx_staging, SPI_AT_TYPE_COMMAND, seq, text);
    if (len >= 0)
    {
        s_tx_dirty = true;
    }
    k_mutex_unlock(&tx_lock);

    if (len < 0)
    {
        return -EINVAL;
    }

    if (out_seq != NULL)
    {
        *out_seq = seq;
    }

    EBADGE_LOG2("wifi8711 xfer: staged ATMC seq=%u len=%d", (unsigned)seq, len);
    return 0;
}

int wifi_8711_xfer_test_wait_slot(uint32_t timeout_ms)
{
    k_sem_reset(&slot_arrived_sem);
    if (k_sem_take(&slot_arrived_sem, K_MSEC(timeout_ms)) != 0)
    {
        return -EAGAIN;
    }
    return 0;
}

#endif /* CONFIG_WIFI_8711 */
