/**
 * @file    ota_cache.c
 * @brief   PSRAM staging buffer for a received 8711 firmware image.  See
 *          ota_cache.h for why the whole image has to be buffered -- that is the
 *          part that is not obvious.
 */
#include <string.h>
#include <errno.h>

#include "ota_cache.h"

#include "../ebadge_log.h"
#include "../port/ebadge_psram_map.h"

/* The window from the shared map, as a pointer.
 *
 * Deliberately not a static array: see the "WHERE IT LIVES" section in the
 * header.  The cast is the whole mechanism by which this buffer ends up in the
 * non-cacheable MPU region, so the address must come from the map header and
 * nowhere else. */
static uint8_t *const s_buf = (uint8_t *)EB_PSRAM_OTA_STAGE_BASE;

/* How much of s_buf is currently occupied. */
static uint32_t s_len;

uint32_t ota_cache_capacity(void)
{
    return EB_PSRAM_OTA_STAGE_SIZE;
}

void ota_cache_reset(void)
{
    s_len = 0;
}

int ota_cache_append(const uint8_t *data, uint16_t len)
{
    if (data == NULL) { return -EINVAL; }
    if (len == 0)     { return 0; }

    /* Checked before the copy: an overrun means the sender has gone past the size
     * it declared in the offer, and the useful outcome is to refuse it with the
     * window intact rather than to notice afterwards, having already written past
     * the end of SPIC3's mapped PSRAM. */
    if (s_len + len > EB_PSRAM_OTA_STAGE_SIZE)
    {
        EBADGE_ERR2("ota_cache: overflow, have=%u add=%d",
                    (unsigned)s_len, (int)len);
        return -ENOSPC;
    }

    memcpy(s_buf + s_len, data, len);
    s_len += len;
    return 0;
}

uint32_t ota_cache_len(void)
{
    return s_len;
}

const uint8_t *ota_cache_data(void)
{
    return (s_len > 0) ? s_buf : NULL;
}
