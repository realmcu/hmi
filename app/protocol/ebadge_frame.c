/**
 * @file    ebadge_frame.c
 * @brief   Wire codec for the V1.2 frame:  ver | cmd | flags | len(LE) | body.
 *
 * The stream reassembler tolerates fragment / batch delivery from BLE ATT
 * writes.  There is no CRC, no magic, no ACK -- the ATT reliability layer
 * (write-with-response) does that for us.
 */
#include <string.h>
#include "ebadge_frame.h"
#include "ebadge_errcode.h"
#include "ebadge_log.h"

/*----------------------------------------------------------------------------*
 *  Encoder
 *----------------------------------------------------------------------------*/
int ebadge_frame_pack(uint8_t cmd, const uint8_t *params, uint16_t params_len,
                      uint8_t *out, uint16_t out_cap)
{
    if (out == NULL || (params == NULL && params_len != 0))
    {
        return EBADGE_ERR_PARAM;
    }
    if (params_len > EBADGE_PARAMS_MAX)
    {
        return EBADGE_ERR_PARAM;
    }
    uint16_t total = (uint16_t)(EBADGE_HDR_LEN + params_len);
    if (out_cap < total)
    {
        return EBADGE_ERR_NOMEM;
    }

    out[0] = EBADGE_VER;
    out[1] = cmd;
    out[2] = EBADGE_FLAG_REQUEST;
    out[3] = (uint8_t)(params_len & 0xFF);        /* len LSB              */
    out[4] = (uint8_t)((params_len >> 8) & 0xFF); /* len MSB              */
    if (params_len)
    {
        memcpy(out + EBADGE_HDR_LEN, params, params_len);
    }
    return (int)total;
}

/*----------------------------------------------------------------------------*
 *  Decoder
 *----------------------------------------------------------------------------*/
void ebadge_frame_rx_reset(ebadge_rx_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return;
    }
    ctx->state    = EBADGE_RX_S_HDR;
    ctx->hdr_got  = 0;
    ctx->body_len = 0;
    ctx->body_got = 0;
}

int ebadge_frame_feed(ebadge_rx_ctx_t *ctx,
                      const uint8_t *bytes, uint16_t len,
                      ebadge_frame_cb_t cb, void *user)
{
    if (ctx == NULL || bytes == NULL || cb == NULL)
    {
        return 0;
    }

    int      frames_out = 0;
    uint16_t i          = 0;

    while (i < len)
    {
        switch (ctx->state)
        {
        case EBADGE_RX_S_HDR:
            {
                uint16_t need = (uint16_t)(EBADGE_HDR_LEN - ctx->hdr_got);
                uint16_t have = (uint16_t)(len - i);
                uint16_t take = (have < need) ? have : need;
                memcpy(ctx->hdr + ctx->hdr_got, bytes + i, take);
                ctx->hdr_got = (uint16_t)(ctx->hdr_got + take);
                i            = (uint16_t)(i + take);
                if (ctx->hdr_got < EBADGE_HDR_LEN)
                {
                    return frames_out; /* need more bytes */
                }

                /* Parse header: ver | cmd | flags | len_lo | len_hi (LE) */
                uint8_t  ver   = ctx->hdr[0];
                uint16_t plen  = (uint16_t)(ctx->hdr[3] |
                                            ((uint16_t)ctx->hdr[4] << 8));
                if (ver != EBADGE_VER || plen > EBADGE_PARAMS_MAX)
                {
                    EBADGE_WARN2("bad hdr ver=%d plen=%d, resync", ver, plen);
                    ebadge_frame_rx_reset(ctx);
                    continue;
                }
                ctx->body_len = plen;
                ctx->body_got = 0;
                ctx->state    = EBADGE_RX_S_BODY;
                /* fall through to body if there are more bytes queued */
                if (plen == 0)
                {
                    /* zero-body: dispatch immediately */
                    cb(ctx->hdr[0], ctx->hdr[1], ctx->hdr[2],
                       NULL, 0, user);
                    frames_out++;
                    ebadge_frame_rx_reset(ctx);
                }
                break;
            }

        case EBADGE_RX_S_BODY:
            {
                uint16_t need = (uint16_t)(ctx->body_len - ctx->body_got);
                uint16_t have = (uint16_t)(len - i);
                uint16_t take = (have < need) ? have : need;
                memcpy(ctx->body + ctx->body_got, bytes + i, take);
                ctx->body_got = (uint16_t)(ctx->body_got + take);
                i             = (uint16_t)(i + take);
                if (ctx->body_got == ctx->body_len)
                {
                    cb(ctx->hdr[0], ctx->hdr[1], ctx->hdr[2],
                       ctx->body, ctx->body_len, user);
                    frames_out++;
                    ebadge_frame_rx_reset(ctx);
                }
                break;
            }
        }
    }
    return frames_out;
}
