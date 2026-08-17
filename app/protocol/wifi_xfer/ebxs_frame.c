/**
 * @file    ebxs_frame.c
 * @brief   Little-endian codec for the EBXS stream-frame header (spec §6.2).
 */
#include <string.h>
#include "ebxs_frame.h"

static uint32_t rd_le32(const uint8_t *p)
{
    return (uint32_t)p[0]
           | ((uint32_t)p[1] << 8)
           | ((uint32_t)p[2] << 16)
           | ((uint32_t)p[3] << 24);
}

int ebxs_hdr_parse(const uint8_t *buf, ebxs_hdr_t *out)
{
    if (!buf || !out) { return -1; }
    /* §6.2 keeps the EBXF magic for stream frames -- not a typo in the spec,
     * and not something we can use to tell the two layouts apart.           */
    if (buf[0] != 'E' || buf[1] != 'B' || buf[2] != 'X' || buf[3] != 'F')
    {
        return -2;
    }
    if (buf[4] != EBXS_VER)
    {
        return -3;
    }
    out->ver        = buf[4];
    out->file_type  = buf[5];
    out->frame_size = rd_le32(buf + 6);
    out->crc32      = rd_le32(buf + 10);

    /* A zero or absurd length would park the session waiting for bytes that
     * never come, so reject it here rather than at the 3s frame timeout.    */
    if (out->frame_size == 0 || out->frame_size > EBXS_MAX_FRAME_BYTES)
    {
        return -4;
    }
    return 0;
}
