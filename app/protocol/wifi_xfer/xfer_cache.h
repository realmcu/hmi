/**
 * @file    xfer_cache.h
 * @brief   PSRAM staging buffer that holds a whole received file before any of it
 *          reaches flash.
 *
 * ---------------------------------------------------------------------------
 * WHY A WHOLE-FILE BUFFER AND NOT A STREAMING WRITE
 * ---------------------------------------------------------------------------
 * The receive path used to append every chunk to flash as it arrived, which put
 * two flash operations on the data path:
 *
 *   - the multi-sector NOR erase in wp_begin, on the WAIT_STA -> RECV edge, and
 *   - a page program per chunk.
 *
 * The erase is the expensive one and it cost real transfers.  The 8711 gives us
 * only 1000 ms of back-pressure (protocol sec.3.2: it abandons a slot whose READY
 * never comes), and a 12-sector erase does not finish inside that, so the slot was
 * dropped -- with the bytes already consumed from TCP, i.e. actually lost rather
 * than merely delayed.  It showed up as a chunk CRC mismatch three chunks later,
 * pointing nowhere near the cause.
 *
 * Buffering the file here moves both operations off the data path entirely: while
 * data is arriving, the only work per chunk is a memcpy and a CRC update, and
 * flash is not touched at all.  The erase and the writes happen after the last
 * chunk, when nothing is in flight and a stall costs nothing.
 *
 * The second thing it buys is that a file is only ever committed once it is known
 * to be good.  Verification happens against the buffer, so a failed transfer
 * never reaches flash -- previously a bad transfer had already been written and
 * had to be un-reserved with wp_abort.
 *
 * ---------------------------------------------------------------------------
 * WHERE IT LIVES, AND WHY THAT IS NOT A STATIC ARRAY
 * ---------------------------------------------------------------------------
 * A fixed window of SPIC1 PSRAM, carved out in ebadge_psram_map.h.  Not a static
 * array, for two reasons:
 *
 *   - 1 MB does not fit anywhere the linker would put it.  DTCM is ~82 KB total.
 *   - The region is inside the non-cacheable MPU window, which is a property of
 *     the address, not of the symbol.  Declaring an array and hoping it landed
 *     there would be exactly the kind of silent drift the map header exists to
 *     stop.
 *
 * Consequence worth knowing: SPIC1 PSRAM only comes up inside
 * app_system_lower_init(), so nothing here may be touched before that.  Every
 * entry point below is reached from the protocol task, which starts long after.
 *
 * CONTEXT: all functions run on l2_task.  There is one transfer at a time, so
 * there is one buffer and no locking.
 */
#ifndef _XFER_CACHE_H_
#define _XFER_CACHE_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Largest file the cache can hold, and therefore the largest this device can
 *  receive.  Announced to the App at offer time as TOO_LARGE rather than
 *  discovered part-way through. */
uint32_t xfer_cache_capacity(void);

/** Drop whatever is buffered.  Cheap -- it only rewinds the cursor, it does not
 *  clear the memory, because every byte read back is a byte that was written
 *  first.  Safe to call when already empty. */
void     xfer_cache_reset(void);

/**
 * @brief  Append received bytes.
 *
 * @retval 0        buffered
 * @retval -EINVAL  bad arguments
 * @retval -ENOSPC  would exceed the capacity -- the sender is past the size it
 *                  declared in its offer.  Nothing was copied.
 */
int      xfer_cache_append(const uint8_t *data, uint16_t len);

/** Bytes buffered so far. */
uint32_t xfer_cache_len(void);

/**
 * @brief  The buffered bytes, for the flash write and the CRC pass.
 *
 * Points into PSRAM, valid until the next xfer_cache_reset().  Read-only by
 * contract: the CRC that was accumulated over these bytes is what makes them
 * trustworthy, so a caller that modified them would invalidate the one check
 * that says they are the file the sender promised.
 *
 * @return NULL if nothing is buffered.
 */
const uint8_t *xfer_cache_data(void);

#ifdef __cplusplus
}
#endif

#endif /* _XFER_CACHE_H_ */
