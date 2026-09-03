/**
 * @file    ota_cache.h
 * @brief   PSRAM staging buffer that holds a whole 8711 firmware image before any
 *          of it is pushed at the module.
 *
 * ---------------------------------------------------------------------------
 * WHY THE WHOLE IMAGE, AND NOT A PASS-THROUGH
 * ---------------------------------------------------------------------------
 * It would be tempting to forward each TCP chunk straight into AT+OTAUSER's
 * transparent-mode stream and never buffer anything.  That does not work, for a
 * reason that is structural rather than a matter of tuning:
 *
 * AT+OTAUSER declares the total length in its command line, before the first byte
 * of firmware exists on this side.  Transparent mode then ends on that count, so
 * the command cannot be opened until the length is known and cannot be paused
 * once opened.  A pass-through would therefore hold the 8711 in transparent mode
 * for the entire duration of a Wi-Fi upload -- during which the phone's own TCP
 * connection is being served by that same 8711, and the AT link that would carry
 * its bytes is the one occupied by the push.  The two cannot overlap.
 *
 * So the phases are serialised, and this buffer is what makes that possible:
 * receive the whole image over Wi-Fi, tear the data plane down, then push.
 *
 * The second thing it buys is the same thing xfer_cache buys -- see its header --
 * namely that the receive path does no slow work per chunk.  The 8711 abandons a
 * slot whose READY does not come back within 1000 ms, so a chunk callback that
 * did anything expensive would lose bytes that had already left TCP.
 *
 * ---------------------------------------------------------------------------
 * WHERE IT LIVES
 * ---------------------------------------------------------------------------
 * A fixed window of SPIC3 PSRAM, carved out in ebadge_psram_map.h as
 * EB_PSRAM_OTA_STAGE_*.  Sized above the vendor's own example image (1120704 B,
 * which is already over 1 MB), because an OTA package that will not fit is an OTA
 * that cannot happen at all -- unlike a wallpaper, there is no smaller version of
 * the firmware to send instead.
 *
 * Not a static array, for the reasons in xfer_cache.h: it does not fit anywhere
 * the linker would put it, and the non-cacheable MPU attribute is a property of
 * the ADDRESS rather than of the symbol.  That attribute is also why the push
 * needs no cache maintenance: at_client memcpy's out of here on the CPU, and
 * MPU region 4 maps SPIC3 non-cacheable, so there is no dirty line to write back.
 *
 * SPIC3 PSRAM only comes up inside app_system_lower_init(), long before the
 * protocol task exists -- so nothing here may be touched earlier, and nothing
 * reaches it earlier.
 *
 * CONTEXT: all functions run on l2_task.  There is one OTA at a time, so there is
 * one buffer and no locking.
 */
#ifndef _OTA_CACHE_H_
#define _OTA_CACHE_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Largest image the cache can hold, and therefore the largest this device can
 *  accept.  Announced at offer time rather than discovered part-way through. */
uint32_t ota_cache_capacity(void);

/** Drop whatever is buffered.  Cheap -- it rewinds the cursor and does not clear
 *  the memory, because every byte read back is a byte that was written first.
 *  Safe to call when already empty. */
void     ota_cache_reset(void);

/**
 * @brief  Append received bytes.
 *
 * @retval 0        buffered
 * @retval -EINVAL  bad arguments
 * @retval -ENOSPC  would exceed the capacity -- the sender is past the size it
 *                  declared.  Nothing was copied.
 */
int      ota_cache_append(const uint8_t *data, uint16_t len);

/** Bytes buffered so far. */
uint32_t ota_cache_len(void);

/**
 * @brief  The buffered image, for the push.
 *
 * Points into PSRAM and stays valid until the next ota_cache_reset() -- which is
 * the contract the push depends on, since at_client borrows this pointer for the
 * whole transfer rather than copying it.  Resetting the cache while a push is in
 * flight would pull the image out from under the wire.
 *
 * @return NULL if nothing is buffered.
 */
const uint8_t *ota_cache_data(void);

#ifdef __cplusplus
}
#endif

#endif /* _OTA_CACHE_H_ */
