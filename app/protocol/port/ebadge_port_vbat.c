/**
 * @file    ebadge_port_vbat.c
 * @brief   VBAT voltage via the ROM aux-ADC manager.  Bring-up instrumentation.
 *
 * ---------------------------------------------------------------------------
 * WHY NOT THE ZEPHYR ADC DRIVER
 * ---------------------------------------------------------------------------
 * Because it cannot reach the battery.  zephyr/drivers/adc/adc_rtl87x3g.c maps
 * its channels onto EXT_SINGLE_ENDED(i) only -- there is no code path to
 * INTERNAL_VBAT_MODE anywhere in it.  So the usual checklist (set the disabled
 * adc@40010000 node okay, add pinctrl, CONFIG_ADC=y) buys nothing here: what is
 * missing is not configuration but a driver feature.  Same for the posix
 * /dev/adc0 port, which sits on top of that driver.
 *
 * VBAT is an internal channel: no pad, no pinctrl, no devicetree.  Reaching it
 * means the vendor ROM API, which is what this file does.
 *
 * ---------------------------------------------------------------------------
 * WHY adc_manager AND NOT ADC_Init/ADC_Read DIRECTLY
 * ---------------------------------------------------------------------------
 * The aux ADC is shared with the charger module, which samples it on its own
 * schedule.  adc_manager is the arbiter: it owns the peripheral config and
 * hands out schedule-table slots.  Programming ADC_Init() behind its back would
 * work right up until the charger reprogrammed the same registers.
 *
 * soc.c already calls adc_mgr_init(sys_init_cfg.adc_mgr_queue) during SoC init,
 * well before l2_task exists, so registration from here is always late enough.
 *
 * ---------------------------------------------------------------------------
 * WHY THIS IS A CACHE AND NOT A GETTER
 * ---------------------------------------------------------------------------
 * A conversion completes on an ADC interrupt, not on return from the submit.
 * The vendor reference (watch/src/dev/charger/cj4056_charger.c) papers over that
 * with an os_delay(10) after adc_mgr_enable_req() and then reads the result --
 * which cannot be copied here: the only caller is the 0x17 GET_BATTERY handler
 * running on l2_task, and blocking that task stalls every BLE command, both
 * Wi-Fi session state machines and all of their timeouts.
 *
 * So the tick submits, the ISR stores, and the reader takes the last value.
 * The cost is that the very first GET_BATTERY after boot may find nothing yet;
 * that is reported as "unknown" rather than as 0 mV, because 0 mV renders as a
 * flat battery on the phone.
 *
 * ---------------------------------------------------------------------------
 * WHAT IS NOT PROVEN BY THIS COMPILING
 * ---------------------------------------------------------------------------
 * Every symbol used here (adc_mgr_register_req, adc_mgr_enable_req, ADC_GetRes,
 * ...) is an ABSOLUTE address in ROM -- `nm` shows them as 'A', not 'T'.  The
 * link therefore succeeds no matter what state the ROM side is in, exactly like
 * the upperstack and charger_api symbols elsewhere in this tree.  A green build
 * says nothing about whether the conversion runs or whether ADC_GetRes() has
 * valid calibration data behind it, which is why the raw code is kept and
 * logged next to the millivolts: a stuck 0/0xfff code means the ADC is not
 * sampling, while a plausible code with implausible mV means calibration.
 */
#include <stdint.h>
#include <stdbool.h>

#include "ebadge_port_vbat.h"
#include "../ebadge_task.h"
#include "../ebadge_log.h"

#include "rtl876x_adc.h"
#include "adc_manager.h"

/** How often to submit a conversion.
 *
 *  Battery voltage moves over minutes, so this is about having a fresh-enough
 *  value on hand when the phone asks, not about tracking anything.  2 s keeps
 *  the reading at most one interval stale while a bring-up session is watching
 *  the log, and is still nowhere near often enough to contend with the charger
 *  for the shared ADC.                                                       */
#define VBAT_SAMPLE_MS          2000u

/** Schedule-table slot we asked for.  One entry, so bit 0.  The read-back call
 *  wants the same value as a bitmap, which is why it is a macro rather than a
 *  literal repeated at both sites -- they must agree or the read returns
 *  another slot's data. */
#define VBAT_SCHED_BITMAP       0x0001u

static bool     s_registered;       /* adc_mgr handed us a channel            */
static uint8_t  s_chan;             /* ...this one                            */
static bool     s_have;             /* at least one conversion has completed  */
static uint16_t s_mv;               /* last result, millivolts                */
static uint16_t s_raw;              /* ...and the code it came from           */
static uint32_t s_next_ms;
static uint32_t s_samples;          /* completed conversions, for the log     */

/*----------------------------------------------------------------------------*
 *  Conversion complete -- ADC interrupt context
 *
 *  Runs in an ISR.  Nothing here may block, allocate or take a mutex.  The
 *  stores are plain: a torn read cannot happen for these widths on Cortex-M,
 *  and the worst case for the s_have/s_mv pair is a reader seeing the previous
 *  sample, which is fine for a value that is at most seconds old anyway.
 *----------------------------------------------------------------------------*/
static void vbat_adc_cb(void *para, uint32_t int_status)
{
    (void)para; (void)int_status;

    uint16_t raw = 0;

    /* Same bitmap that was registered.  Passing a different one here reads a
     * slot we never configured and yields another channel's sample. */
    if (!adc_mgr_read_data_req(s_chan, &raw, VBAT_SCHED_BITMAP))
    {
        return;
    }

    /* ADC_GetRes() applies the per-chip calibration and returns millivolts.
     * The mode argument must match the schedule entry -- handing it
     * EXT_SINGLE_ENDED would apply the wrong gain and produce a plausible but
     * wrong voltage, which is the hardest kind of wrong to notice. */
    int32_t mv = ADC_GetRes(raw, INTERNAL_VBAT_MODE);

    s_raw = raw;
    s_mv  = (mv < 0) ? 0U : (uint16_t)mv;
    s_have = true;
    s_samples++;
}

/*----------------------------------------------------------------------------*
 *  Tick -- on l2_task
 *----------------------------------------------------------------------------*/
static void on_tick(uint32_t now_ms)
{
    if (!s_registered)
    {
        return;
    }
    if ((int32_t)(now_ms - s_next_ms) < 0)
    {
        return;
    }
    s_next_ms = now_ms + VBAT_SAMPLE_MS;

    /* One-shot.  The result arrives on vbat_adc_cb(); a failure here is not
     * retried early -- the next tick is the retry, and logging per failure
     * would be one line every 2 s for the life of the device. */
    (void)adc_mgr_enable_req(s_chan);
}

/*----------------------------------------------------------------------------*
 *  Public API
 *----------------------------------------------------------------------------*/
void ebadge_port_vbat_init(void)
{
    if (s_registered)
    {
        return;
    }

    ADC_InitTypeDef init;

    /* StructInit first: the struct carries ~20 fields (FIFO thresholds, power-on
     * delays, averaging) and only the three below are ours to choose.  Filling
     * it by hand would leave the rest at whatever was on the stack. */
    ADC_StructInit(&init);
    init.adcClock    = ADC_CLK_39K;
    init.schIndex[0] = INTERNAL_VBAT_MODE;
    init.bitmap      = VBAT_SCHED_BITMAP;

    if (!adc_mgr_register_req(&init, (adc_callback_function_t)vbat_adc_cb,
                              &s_chan))
    {
        /* Say so once and stay off the ADC.  The reader then reports "unknown",
         * which is the truth -- as opposed to reporting 0 mV, which the phone
         * would show as an empty battery. */
        EBADGE_WARN("port_vbat: adc_mgr_register_req FAILED -- no battery"
                    " voltage will be available");
        return;
    }

    s_registered = true;
    ebadge_task_set_tick(on_tick);
    /* Sample on the next tick rather than after a full interval, so the first
     * GET_BATTERY has a chance of finding a reading instead of waiting out
     * VBAT_SAMPLE_MS. */
    s_next_ms = ebadge_task_now_ms();

    EBADGE_LOG("port_vbat: INTERNAL_VBAT_MODE registered on adc_mgr chan=%u,"
               " sampling every %u ms", (unsigned)s_chan,
               (unsigned)VBAT_SAMPLE_MS);
    /* Spell out that a successful registration is not a successful reading:
     * every ROM symbol involved is an absolute address, so this far can be
     * reached on a board where the ADC never converts. */
    EBADGE_LOG("port_vbat: registration only proves the ROM API accepted the"
               " request -- watch for the first sample line below");
}

bool ebadge_port_vbat_mv(uint16_t *out_mv)
{
    if (!s_have)
    {
        return false;
    }
    if (out_mv != NULL)
    {
        *out_mv = s_mv;
    }
    return true;
}

uint16_t ebadge_port_vbat_raw(void)
{
    return s_raw;
}

uint32_t ebadge_port_vbat_samples(void)
{
    return s_samples;
}
