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

/* Arrival time of the previous slot, for the cadence measurement.  Separate
 * valid flag rather than a 0 sentinel: uptime 0 is a legitimate value, and more
 * to the point the first completion has no predecessor to be measured against --
 * without this the whole idle stretch since boot is reported as one interval. */
static uint32_t s_last_slot_ms;
static bool     s_last_slot_ms_valid;

static wifi_8711_xfer_stats_t s_stats;

/* Dump every inbound slot's head.  Off by default -- see
 * wifi_8711_xfer_set_rx_dump() for why this is runtime and not compile-time. */
static bool s_rx_dump;

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

    /* Sample W2B immediately after, while the 8711 has had no chance to react
     * to the edge above.  Phase 5 has it releasing W2B only once it observes
     * our B2W fall, so the level here should be 1 every single time; a 0 means
     * it let go early and its next rising edge cannot be told apart from the
     * tail of this one.  Read via the GPIO, which is ISR-safe, rather than
     * inferred -- the whole point is to catch the case where the peer disagrees
     * with the state machine we think we are running. */
    if (wifi_8711_get_request() == 0)
    {
        s_stats.rxdone_w2b_low++;
    }

    s_last_result = result;

    /* Timestamp the arrival HERE, in the completion ISR, rather than in the
     * transport thread.  What we are measuring is the 8711's own cadence, and
     * the thread wakes only after the scheduler gets round to it -- at priority
     * 5 with the GUI running that adds jitter of its own, which would be
     * indistinguishable from jitter on the peer's side.
     *
     * Uptime in ms, not k_cycle_get_32(): the intervals here are seconds, and a
     * 32-bit cycle counter wraps well inside a single idle stretch, which would
     * turn a long gap into a bogusly short one.  10 ms tick granularity is
     * irrelevant against a multi-second interval. */
    uint32_t now = k_uptime_get_32();
    if (s_last_slot_ms_valid)
    {
        uint32_t gap = now - s_last_slot_ms;

        s_stats.slot_gap_ms_last = gap;
        s_stats.slot_gaps++;
        if (gap > s_stats.slot_gap_ms_max)
        {
            s_stats.slot_gap_ms_max = gap;
        }
        /* min starts unset rather than at 0, which would never be beaten. */
        if (s_stats.slot_gap_ms_min == 0U || gap < s_stats.slot_gap_ms_min)
        {
            s_stats.slot_gap_ms_min = gap;
        }
    }
    s_last_slot_ms       = now;
    s_last_slot_ms_valid = true;

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
        /* Counted, not logged: the retry loop below logs once per outage
         * instead, because a driver that keeps rejecting the arm would
         * otherwise flood the log at retry rate. */
        s_stats.arm_fail++;
        return rc;
    }

    s_stats.arms++;

    /* DMA is live -> B2W may go high.  Unless the layer above is holding the
     * link low across a JPU decode + LCDC transfer (sec.3.3 layer 2), in which
     * case the rising edge is its to give, not ours. */
    if (!s_paused)
    {
        /* Sample W2B before the edge, not after: this is the "are we in phase"
         * question.  Phase 6 has us waiting for W2B low before re-arming, so a
         * high level here means we are about to promise READY while the 8711
         * still considers the previous slot outstanding -- exactly the shape of
         * a link that has slipped a phase and then wedges for 1000 ms at a
         * time.  Counted rather than logged: on a slipped link every slot hits
         * it, and a line per slot would bury the surrounding evidence. */
        if (wifi_8711_get_request() == 1)
        {
            s_stats.arm_w2b_high++;
        }

        rc = wifi_8711_set_ready(true);
        if (rc != 0)
        {
            EBADGE_ERR1("wifi8711 xfer: b2w raise failed %d", rc);
        }
    }
    return 0;
}

/*----------------------------------------------------------------------------*
 *  Arm, retrying until it succeeds.  For the transport thread only.
 *
 *  The thread MUST NOT go back to its k_sem_take() with no DMA armed: nothing
 *  would ever complete, so the semaphore would never be given, the thread would
 *  block forever and B2W would stay low -- a silently dead link whose only
 *  symptom is arm_fail == 1.  That was the behaviour before this function
 *  existed, and it is why "back off and hope" is not good enough here.
 *
 *  Retrying without a bound is the right shape rather than a bounded attempt
 *  count: the usual cause is the driver still holding ctx->lock from the
 *  completion we were just woken by, which clears on its own in well under a
 *  millisecond.  If it somehow never clears, an unbounded retry still recovers
 *  the link the moment it does, where a bounded one would have given up for
 *  good.  Meanwhile the 8711 just sees B2W low and waits.
 *----------------------------------------------------------------------------*/
#define ARM_RETRY_DELAY_MS   10U
#define ARM_RETRY_COMPLAIN   100U  /* ~1 s between repeats of the same moan */

static void arm_slot_retry(void)
{
    uint32_t tries = 0U;

    while (arm_slot() != 0)
    {
        if ((tries % ARM_RETRY_COMPLAIN) == 0U)
        {
            EBADGE_ERR1("wifi8711 xfer: arm failed, retrying (arm_fail=%u)",
                        (unsigned)s_stats.arm_fail);
        }
        tries++;
        k_msleep(ARM_RETRY_DELAY_MS);
    }

    if (tries != 0U)
    {
        EBADGE_LOG1("wifi8711 xfer: re-armed after %u failed attempts",
                    (unsigned)tries);
    }
}

/*----------------------------------------------------------------------------*
 *  Wait for W2B to fall (sec.7.2 PARSE gate).
 *
 *  Polled rather than driven off the W2B interrupt on purpose.  The edge
 *  arrives microseconds after our B2W drop, so a short spin catches essentially
 *  every case without the cross-module ISR plumbing a semaphore handoff would
 *  need; and the slow path only runs when the link is already misbehaving.
 *  The interrupt in wifi_8711.c stays as the edge counter for diagnosis.
 *
 *  The elapsed time is recorded even on success, because "the edge came but
 *  late" and "the edge never came" need different fixes and w2b_wait_to alone
 *  cannot separate them -- it only counts the outright timeouts.
 *
 *  MEASURED with the cycle counter, and the deadline enforced against it too,
 *  rather than counting k_msleep(1) iterations.  CONFIG_SYS_CLOCK_TICKS_PER_SEC
 *  is 100 on this board and CONFIG_TICKLESS_KERNEL is off, so a 1 ms sleep
 *  actually parks until the next 10 ms tick.  Inferring the elapsed time from
 *  the loop index therefore under-reports it by up to 10x, and -- worse -- a
 *  loop of W2B_LOW_TIMEOUT_MS such sleeps runs for ~10 s, not the 1 s the name
 *  promises.  Both of those were live bugs found by this instrumentation.
 *----------------------------------------------------------------------------*/

/** Below this, "late" is just tick granularity and not worth a line.  One tick
 *  is 10 ms here, so anything under a few ticks says nothing about the peer. */
#define W2B_FALL_WARN_US   50000U

static inline uint32_t elapsed_us(uint32_t t0_cyc)
{
    /* Unsigned wrap is correct and intentional: the counter is 32-bit and the
     * windows here are at most a second, far short of its period. */
    return k_cyc_to_us_floor32(k_cycle_get_32() - t0_cyc);
}

static void note_fall_time(uint32_t us)
{
    s_stats.w2b_fall_us_last = us;
    if (us > s_stats.w2b_fall_us_max)
    {
        s_stats.w2b_fall_us_max = us;
    }
}

static bool wait_w2b_low(void)
{
    uint32_t t0 = k_cycle_get_32();

    if (wifi_8711_get_request() == 0)
    {
        note_fall_time(0U);
        return true;
    }

    while (elapsed_us(t0) < SLOT_WAIT_SPIN_US)
    {
        k_busy_wait(10);
        if (wifi_8711_get_request() == 0)
        {
            note_fall_time(elapsed_us(t0));
            return true;
        }
    }

    while (elapsed_us(t0) < W2B_LOW_TIMEOUT_MS * 1000U)
    {
        k_msleep(1);
        if (wifi_8711_get_request() == 0)
        {
            uint32_t us = elapsed_us(t0);
            note_fall_time(us);
            if (us >= W2B_FALL_WARN_US)
            {
                /* Genuinely slow, not quantisation.  The precursor to the
                 * timeout below rather than a separate fault, so it is worth a
                 * line -- but only past the threshold, otherwise every single
                 * slot logs and the surrounding evidence is buried. */
                EBADGE_WARN1("wifi8711 xfer: W2B fell late, after %u us",
                             (unsigned)us);
            }
            return true;
        }
    }

    note_fall_time(elapsed_us(t0));
    s_stats.w2b_wait_to++;

    /* sec.3.2: the 8711 gives up after 1000 ms too.  Do not force a slot
     * through -- carry on to the next ARM and let the counter show it.
     *
     * Dump the surrounding state rather than just the fact: which side stopped
     * driving is not deducible from "stuck high" alone.  The edge counts are the
     * discriminator -- rising == falling + 1 and frozen means the 8711 asserted
     * and never released, while rising == falling means we are reading a level
     * that already toggled and the poll simply missed it. */
    uint32_t rising = 0U, falling = 0U;
    wifi_8711_get_w2b_stats(&rising, &falling);
    EBADGE_WARN2("wifi8711 xfer: W2B stuck high %u us, re-arming anyway (x%u)",
                 (unsigned)s_stats.w2b_fall_us_last, (unsigned)s_stats.w2b_wait_to);
    EBADGE_WARN2("wifi8711 xfer:   w2b edges rising=%u falling=%u",
                 (unsigned)rising, (unsigned)falling);
    EBADGE_WARN2("wifi8711 xfer:   b2w=%d arms=%u",
                 wifi_8711_get_ready(), (unsigned)s_stats.arms);
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

            /* Before the sink, so the bytes are on the console even when the
             * parser below rejects them -- a slot that fails its magic or CRC
             * check is exactly the one worth seeing, and after the sink it may
             * already have been dropped with nothing but a counter to show. */
            if (s_rx_dump)
            {
                ebadge_log_hexdump(EB_DIR_FROM_8711 "slot", slot_rx[done_idx],
                                   WIFI_8711_SLOT_SIZE, EBADGE_HEXDUMP_DEFAULT);
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

        /* Must not fall through with nothing armed -- see arm_slot_retry(). */
        arm_slot_retry();
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
    s_last_slot_ms_valid = false;
    memset(&s_stats, 0, sizeof(s_stats));
    /* Not left at the memset's 0: that reads as "W2B was low at start", which is
     * the healthy case and the opposite of what a 0 would actually mean here. */
    s_stats.w2b_at_start = -1;

    memset(slot_tx, 0, sizeof(slot_tx));

    /* Keep anything staged BEFORE the transport started, so it rides the very
     * first slot.  Wiping unconditionally here used to cost a whole POLL period
     * (up to ~2 s): the first slot went out all-zero and the caller's command
     * could only be promoted by the *second* arm.  Staging first and starting
     * second is now the fast path -- see wifi_8711_at_query.c.
     *
     * Idle content is all zeroes, which sec.4.2 explicitly allows; the 8711
     * simply finds no valid ATMC COMMAND on MISO. */
    if (!s_tx_dirty)
    {
        memset(slot_tx_staging, 0, sizeof(slot_tx_staging));
    }

    /* Arm the FIRST slot before the thread exists.  If the 8711 booted ahead
     * of us its opening W2B edge is already gone, and a transport that only
     * armed on an edge would sit there forever -- the latent bug in the
     * bt_audio_trx reference, which never pre-arms.
     *
     * Not arm_slot_retry(): with no thread yet there is nobody to be deadlocked,
     * so a failure here is better reported to the caller than retried silently. */
    s_stats.w2b_at_start = wifi_8711_get_request();
    if (s_stats.w2b_at_start == 1)
    {
        /* The 8711 is mid-request already: it raised W2B and is counting down its
         * 1000 ms READY timeout (sec.3.2).  Our B2W rising edge is about to land
         * inside that window rather than at the head of a clean cycle, which is
         * legal -- that edge is exactly what it is waiting for -- but it also
         * means slot one runs with no idea how much of the 8711's timeout is
         * already spent.  Logged because it is the first thing to rule out when
         * the on-demand AT path fails while the idle POLL path is fine. */
        EBADGE_WARN("wifi8711 xfer: W2B already high at start -- pre-arming into "
                    "a request the 8711 has already made");
    }
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

    /* Deliberately NOT gated on s_started: staging before the transport starts
     * is the fast path, because wifi_8711_xfer_start() keeps staged content and
     * arms it into the very first slot.  Requiring "started" first would force
     * the caller to waste a whole POLL period on an all-zero slot. */
    k_mutex_lock(&tx_lock, K_FOREVER);
    memcpy(slot_tx_staging, slot, WIFI_8711_SLOT_SIZE);
    s_tx_dirty = true;
    k_mutex_unlock(&tx_lock);
    return 0;
}

int wifi_8711_xfer_set_tx_idle(void)
{
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
    /* Sampled before the wipe: it describes how the transport started, not
     * anything that has happened since, so a reset must not lose it.  Zeroing
     * it would read as "W2B was low at start" -- the healthy case, and a lie. */
    int32_t at_start = s_started ? s_stats.w2b_at_start : -1;

    memset(&s_stats, 0, sizeof(s_stats));
    s_stats.w2b_at_start = at_start;

    /* Drop the cadence baseline too, so the first interval after a reset is a
     * real slot-to-slot gap rather than the whole stretch since the last slot
     * before it -- which on an idle link would be reported as a huge max and
     * read as a lost transaction. */
    s_last_slot_ms_valid = false;
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

void wifi_8711_xfer_set_rx_dump(bool on)
{
    /* A plain bool store, no lock: the transport thread only reads it, and the
     * worst a race can do is dump or skip one more slot than asked for. */
    s_rx_dump = on;
}

bool wifi_8711_xfer_rx_dump(void)
{
    return s_rx_dump;
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
    /* No s_started gate: see wifi_8711_xfer_set_tx().  Staging the command
     * first and starting the transport second saves a whole POLL period. */
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
