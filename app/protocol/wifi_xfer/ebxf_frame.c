/**
 * @file    ebxf_frame.c
 * @brief   Little-endian codec for the EBXF header + EBXR ack (spec §5.2/§5.3).
 */
#include <string.h>
#include "ebxf_frame.h"

static uint32_t rd_le32(const uint8_t *p)
{
    return (uint32_t)p[0]
           | ((uint32_t)p[1] << 8)
           | ((uint32_t)p[2] << 16)
           | ((uint32_t)p[3] << 24);
}

int ebxf_hdr_parse(const uint8_t *buf, ebxf_hdr_t *out)
{
    if (!buf || !out) { return -1; }
    if (buf[0] != 'E' || buf[1] != 'B' || buf[2] != 'X' || buf[3] != 'F')
    {
        return -2;
    }
    if (buf[4] != EBXF_VER)
    {
        return -3;                          /* unknown EBXF version          */
    }
    out->ver       = buf[4];
    out->file_type = buf[5];
    out->name_len  = buf[6];
    /* buf[7]  reserved */
    out->file_size = rd_le32(buf + 8);
    out->crc32     = rd_le32(buf + 12);

    /* Copy the whole 24-byte field, then honour name_len as the authoritative
     * cut so a sender that leaves trailing garbage cannot leak it into the
     * stored file name.  An out-of-range name_len falls back to the field
     * width and relies on the NUL we always append.                        */
    memcpy(out->file_name, buf + 16, EBXF_NAME_LEN);
    out->file_name[EBXF_NAME_LEN] = '\0';
    if (out->name_len <= EBXF_NAME_LEN)
    {
        out->file_name[out->name_len] = '\0';
    }
    return 0;
}

void ebxr_pack(uint8_t out[EBXR_LEN], uint8_t status, uint8_t reason)
{
    if (!out) { return; }
    out[0] = 'E';
    out[1] = 'B';
    out[2] = 'X';
    out[3] = 'R';
    out[4] = status;
    out[5] = reason;
    out[6] = 0;                             /* reserved u16 LE = 0x0000      */
    out[7] = 0;
}
