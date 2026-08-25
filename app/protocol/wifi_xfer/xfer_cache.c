/**
 * @file    xfer_cache.c
 * @brief   PSRAM staging buffer for received files.  See xfer_cache.h for why
 *          this exists at all -- the rationale is the interesting part.
 */
#include <string.h>
#include <errno.h>

#include "xfer_cache.h"

#include "../ebadge_log.h"
#include "../port/ebadge_psram_map.h"

/* The window from the shared map, as a pointer.
 *
 * Deliberately not a static array: see the "WHERE IT LIVES" section in the
 * header.  The cast is the whole mechanism by which this buffer ends up in the
 * non-cacheable MPU region, so the address must come from the map header and
 * nowhere else. */
static uint8_t *const s_buf = (uint8_t *)EB_PSRAM_XFER_CACHE_BASE;

/* How much of s_buf is currently occupied.  This, not the memory contents, is
 * what reset() clears -- there is no state here beyond the cursor. */
static uint32_t s_len;

uint32_t xfer_cache_capacity(void)
{
    return EB_PSRAM_XFER_CACHE_SIZE;
}

void xfer_cache_reset(void)
{
    s_len = 0;
}

int xfer_cache_append(const uint8_t *data, uint16_t len)
{
    if (data == NULL) { return -EINVAL; }
    if (len == 0)     { return 0; }

    /* Checked before the copy, not after: an overrun here is a sender that has
     * exceeded its own declared size, and the interesting thing is to refuse it
     * with the buffer still intact rather than to detect it having already
     * scribbled past the end of the window and into psram1_nc. */
    if (s_len + len > EB_PSRAM_XFER_CACHE_SIZE)
    {
        EBADGE_ERR2("xfer_cache: overflow, have=%u add=%d",
                    (unsigned)s_len, (int)len);
        return -ENOSPC;
    }

    memcpy(s_buf + s_len, data, len);
    s_len += len;
    return 0;
}

uint32_t xfer_cache_len(void)
{
    return s_len;
}

const uint8_t *xfer_cache_data(void)
{
    return (s_len > 0) ? s_buf : NULL;
}
