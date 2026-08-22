/**
 * @file    ebadge_port_vbat.h
 * @brief   Battery rail (VBAT) voltage, read through the ROM aux-ADC manager.
 *
 * VBAT is an INTERNAL ADC channel: it needs no external pad, no pinctrl entry
 * and no devicetree node.  This is why the Zephyr ADC driver is not used --
 * adc_rtl87x3g.c only exposes EXT_SINGLE_ENDED(i) and has no code path to the
 * internal mode at all, so enabling CONFIG_ADC and the disabled adc@40010000
 * node would still not reach the battery.  See ebadge_port_vbat.c for the rest
 * of the rationale.
 *
 * The read is ASYNCHRONOUS and this API is deliberately a cache reader, not a
 * blocking getter: the only caller is a BLE command handler on l2_task, which
 * must not sleep.  A periodic tick submits conversions; the ADC interrupt
 * stores the result; ebadge_port_vbat_mv() hands back whatever the last
 * conversion produced.
 */
#ifndef _EBADGE_PORT_VBAT_H_
#define _EBADGE_PORT_VBAT_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Register the VBAT channel with the ROM ADC manager and start the
 *         refresh tick.  Idempotent; safe to call when the ADC is unavailable
 *         (it logs and leaves ebadge_port_vbat_mv() reporting "no reading").
 *
 * Must run after the SoC's adc_mgr_init(), which soc.c does during its own
 * init -- long before l2_task exists, so any call site inside the protocol
 * stack is late enough.
 */
void ebadge_port_vbat_init(void);

/**
 * @brief  Last completed VBAT conversion, in millivolts.
 *
 * @param[out] out_mv  written only when a reading exists.  May be NULL.
 * @return true if a conversion has ever completed, false while still waiting
 *         for the first one (or if registration failed).  A false return means
 *         "unknown", NOT "0 mV" -- the distinction matters because 0 mV would
 *         otherwise be rendered as a flat battery.
 */
bool ebadge_port_vbat_mv(uint16_t *out_mv);

/**
 * @brief  Raw ADC code behind the last reading, for bring-up only.
 *         Meaningless without the matching ADC_GetRes() conversion, but it is
 *         what separates "the ADC is not sampling" (code stuck at 0 or 0xfff)
 *         from "the ADC samples but the calibration is wrong" (plausible code,
 *         implausible mV).
 */
uint16_t ebadge_port_vbat_raw(void);

/**
 * @brief  Number of completed conversions since boot.
 *
 * Zero while registered is the interesting case: it says the ROM accepted the
 * request and the tick is submitting, but no interrupt ever came back -- i.e.
 * the ADC is not converting, as opposed to converting badly.
 */
uint32_t ebadge_port_vbat_samples(void);

#ifdef __cplusplus
}
#endif

#endif /* _EBADGE_PORT_VBAT_H_ */
