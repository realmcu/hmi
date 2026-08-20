/**
 * @file    wifi_8711_xfer.h
 * @brief   Slot transport for the 8711FA SPI link -- raw send/recv + test API.
 *
 * SCOPE: this layer owns exactly one thing -- getting a fixed 4096-byte
 * full-duplex slot across the wire under the four-phase B2W/W2B handshake, and
 * handing the received bytes to whoever asked for them.  It does NOT parse
 * JPGS or ATMC, does not reassemble JPEG frames, and does not know what an AT
 * command is.  Those sit above and are still unwritten.
 *
 * Splitting it this way is deliberate: the handshake and the DMA re-arm are
 * where the timing bugs live, and they can be proven correct with a byte
 * pattern long before a parser exists.  Hence the test API at the bottom.
 *
 * ---------------------------------------------------------------------------
 * The one architectural constraint worth knowing before reading the .c
 * ---------------------------------------------------------------------------
 * The SPI completion callback CANNOT re-arm the next transfer.  In
 * spi_context_complete() the user callback is invoked at spi_context.h:201 but
 * the context lock is only released at :206 -- after the callback returns.  The
 * callback runs in the DMA RX ISR (spi_rtl87x3g.c:402 -> :432 -> :270), so
 * re-entering spi_transceive_cb() there would call k_sem_take(K_FOREVER) from
 * an ISR.  The driver also k_malloc()s when a buffer is NULL
 * (spi_rtl87x3g.c:324/335), which is likewise not ISR-safe.
 *
 * Therefore: the ISR drops B2W and signals; a dedicated transport thread does
 * the re-arm.  That is the only correct shape on this driver.
 *
 * ---------------------------------------------------------------------------
 * Slot lifecycle (protocol sec.3.1 / sec.7.2)
 * ---------------------------------------------------------------------------
 *   ARM     thread: W2B low? arm RX+TX DMA (4096 B each), then raise B2W
 *   READY   the 8711 raises W2B, drives CS/SCLK, clocks 4096 bytes
 *   RX_DONE ISR: drop B2W *first*, then signal the thread
 *   PARSE   thread: wait W2B low, deliver the slot, then back to ARM
 *
 * B2W high always means the same thing -- "8773 is idle, DMA re-armed, send
 * me the next slot".  Nothing here interprets an END flag; the frame-level
 * hold (keep B2W low across JPU decode + LCDC) belongs to the layer above and
 * is expressed through wifi_8711_xfer_pause()/resume().
 */
#ifndef _WIFI_8711_XFER_H_
#define _WIFI_8711_XFER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wifi_8711.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Callback delivering one received slot, in transport-thread context.
 *
 * @param  rx    the 4096 received bytes (valid only for the duration of the
 *               call -- copy anything you need to keep)
 * @param  len   always WIFI_8711_SLOT_SIZE on success
 *
 * Runs on the transport thread, NOT in an ISR, so it may log and take locks.
 * It must still return promptly: B2W stays low for its whole duration, which
 * back-pressures the 8711 (exactly the mechanism the frame-level flow control
 * in sec.3.3 relies on).
 */
typedef void (*wifi_8711_slot_cb_t)(const uint8_t *rx, size_t len);

/**
 * @brief  Start the slot transport: spawn the thread and arm the first slot.
 *
 * wifi_8711_init() must have succeeded first (pads configured, W2B armed).
 * Arming the first slot here rather than waiting for a W2B edge is deliberate:
 * if the 8711 boots faster than we do, the first edge is already gone, and a
 * transport that only arms on an edge would wait forever.  The bt_audio_trx
 * reference implementation has exactly that latent bug -- it never pre-arms.
 *
 * @param  cb  slot sink, or NULL to only count slots (test/bring-up mode)
 * @retval 0        thread started, first slot armed, B2W raised
 * @retval -ENODEV  wifi_8711_init() has not run
 * @retval <0       spi/gpio error from the first arm
 */
int wifi_8711_xfer_start(wifi_8711_slot_cb_t cb);

/** True once the transport thread is running. */
bool wifi_8711_xfer_started(void);

/**
 * @brief  Install (or replace) the slot sink after the transport is running.
 *
 * wifi_8711_xfer_start() takes a sink, but whoever starts the transport is not
 * necessarily whoever wants the bytes -- the shell's `wifi8711 xfer` starts it
 * with NULL.  Without this a later caller would arm nothing, see no error, and
 * silently never receive a slot.
 *
 * @param  cb  new sink, or NULL to stop delivering
 * @retval 0        installed
 * @retval -ENODEV  transport not started (the sink is still recorded, so it
 *                  takes effect once it does start)
 */
int wifi_8711_xfer_set_sink(wifi_8711_slot_cb_t cb);

/**
 * @brief  Stage the next TX slot (what the 8711 will clock out of MISO).
 *
 * Copies @p slot into the TX DMA buffer.  Safe to call at any time: the copy
 * lands in a staging buffer and is promoted into the DMA buffer by the
 * transport thread while no transfer is in flight, which is what keeps us on
 * the right side of "never rewrite the TX slot while TX DMA is running"
 * (sec.4.2 / sec.11.2).
 *
 * @param  slot  exactly WIFI_8711_SLOT_SIZE bytes
 * @retval 0        staged
 * @retval -ENODEV  transport not started
 * @retval -EINVAL  NULL slot
 */
int wifi_8711_xfer_set_tx(const uint8_t *slot);

/** Fill the TX slot with zeroes (the "idle content" of sec.4.2). */
int wifi_8711_xfer_set_tx_idle(void);

/**
 * @brief  Hold B2W low after the current slot / release it again.
 *
 * This is the frame-level flow control of sec.3.3 layer ②: after a JPEG END
 * slot the display path calls pause(), does JPU decode + LCDC, then resume().
 * The rising edge that resume() produces is the 8711's ONLY trigger for
 * DONE <seq> to the phone, so calling resume() early does not fail loudly --
 * it silently breaks end-to-end flow control.
 *
 * pause() does not abort a transfer already in flight; it takes effect at the
 * next ARM.
 */
int wifi_8711_xfer_pause(void);
int wifi_8711_xfer_resume(void);

/*----------------------------------------------------------------------------*
 *  Statistics -- the bring-up instrument
 *
 *  Every counter here answers a specific "which half is broken" question,
 *  which is why they are separate rather than one error count.
 *----------------------------------------------------------------------------*/
typedef struct
{
    uint32_t slots_ok;      /**< completions reporting a full 4096 B         */
    uint32_t slots_short;   /**< completed but < 4096 -- clock/mode mismatch */
    uint32_t slots_err;     /**< negative result from the driver             */
    uint32_t arms;          /**< successful re-arms                          */
    uint32_t arm_fail;      /**< spi_transceive_cb() rejected the arm        */
    uint32_t w2b_wait_to;   /**< timed out waiting for W2B to go low         */
    uint32_t rx_nonzero;    /**< slots whose RX was not all-zero            */
    uint32_t last_result;   /**< raw `result` of the last completion         */
} wifi_8711_xfer_stats_t;

void wifi_8711_xfer_get_stats(wifi_8711_xfer_stats_t *out);
void wifi_8711_xfer_reset_stats(void);

/**
 * @brief  Copy out the most recently received slot, for inspection.
 *
 * @param  dst  destination
 * @param  len  bytes to copy (clamped to WIFI_8711_SLOT_SIZE)
 * @return bytes copied, or 0 if no slot has arrived yet
 */
size_t wifi_8711_xfer_peek_rx(uint8_t *dst, size_t len);

/*----------------------------------------------------------------------------*
 *  Test API
 *
 *  These exist to answer, without any parser in the picture: "does a slot get
 *  across at all, and are the bytes the ones we sent?"
 *
 *  Note what is NOT here: a loopback self-test.  We are the slave and cannot
 *  generate a clock, so nothing moves on this bus unless the 8711 drives it.
 *  Every test below needs a live peer -- there is no way around that, and
 *  pretending otherwise would just produce a test that passes on a dead board.
 *----------------------------------------------------------------------------*/

/**
 * @brief  Stage a recognisable byte pattern as the TX slot.
 *
 * Layout: "8773TEST" + 4-byte LE seed + 4-byte LE length, then a byte ramp
 * (i + seed) & 0xFF for the remainder.  Trivially recognisable in a logic
 * analyser trace and on the 8711's console, and any bit error shows up as a
 * broken ramp rather than plausible-looking data.
 *
 * @param  seed  varies the ramp so consecutive test slots differ
 */
int wifi_8711_xfer_test_pattern(uint32_t seed);

/**
 * @brief  Stage a real ATMC COMMAND slot (the first traffic the 8711 answers).
 *
 * Builds a valid ATMC packet with an auto-incrementing non-zero Sequence and
 * the payload CRC32 over the payload only, per sec.6.  Only two commands
 * exist in the 8711's firmware today: "AT+WLSTATE\r\n" and "AT+WLSTARTAP\r\n".
 *
 * Per sec.4.2 the slot stays staged until a matching RESPONSE arrives, because
 * the 8711 may read the same COMMAND out of several consecutive slots and
 * de-duplicates on Sequence.
 *
 * @param  text  AT command text including the trailing CRLF; must be shorter
 *               than WIFI_8711_AT_COMMAND_MAX (the 8711's parse buffer is
 *               128 B and silently drops anything longer, sec.9)
 * @param  out_seq  optional: receives the Sequence used, for response matching
 * @retval 0        staged
 * @retval -EMSGSIZE  payload >= 128 B
 */
int wifi_8711_xfer_test_at(const char *text, uint32_t *out_seq);

/**
 * @brief  Block until a slot arrives, for a synchronous "did anything move?".
 *
 * @param  timeout_ms  how long to wait
 * @retval 0          a slot arrived
 * @retval -EAGAIN    nothing arrived in time (the expected result against a
 *                    peer whose firmware is not running)
 */
int wifi_8711_xfer_test_wait_slot(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* _WIFI_8711_XFER_H_ */
