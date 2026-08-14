/**
 * @file    ebadge_frame.c
 * @brief   Wire codec for the V1.2 frame:  ver | cmd | flags | len(LE) | body.
 *
 * The stream reassembler tolerates fragment / batch delivery from BLE ATT
 * writes.  There is no CRC, no magic, no ACK -- the ATT reliability layer
 * (write-with-response) does that for us.
 *
 * Beyond plain framing there is one pass-through mode: 0x02 SEND_FILE (spec
 * §4.2) puts its file body on the stream *unframed*, right behind the
 * metadata frame.  Its handler calls ebadge_frame_expect_raw() to divert the
 * next N bytes to a sink; see EBADGE_RX_S_RAW handling in the feed loop.
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
    ctx->state       = EBADGE_RX_S_HDR;
    ctx->hdr_got     = 0;
    ctx->body_len    = 0;
    ctx->body_got    = 0;
    /* Drop any half-received raw body too: a reset means the link went away
     * or we lost sync, so the remaining byte count is meaningless.          */
    ctx->in_frame_cb = false;
    ctx->raw_armed   = false;
    ctx->raw_remain  = 0;
    ctx->raw_cb      = NULL;
    ctx->raw_user    = NULL;
}

int ebadge_frame_expect_raw(ebadge_rx_ctx_t *ctx, uint32_t len,
                            ebadge_raw_cb_t cb, void *user)
{
    if (ctx == NULL || cb == NULL || len == 0)
    {
        return EBADGE_ERR_PARAM;
    }
    /* Only legal from inside a frame callback: the arm is consumed by the
     * feed loop right after the callback returns, so calling it from
     * anywhere else would silently do nothing (or worse, hijack a later
     * frame's body).                                                       */
    if (!ctx->in_frame_cb)
    {
        EBADGE_ERR("expect_raw: not in a frame callback, ignored");
        return EBADGE_ERR_INTERNAL;
    }
    if (ctx->raw_armed)
    {
        EBADGE_ERR("expect_raw: already armed, ignored");
        return EBADGE_ERR_BUSY;
    }
    ctx->raw_armed  = true;
    ctx->raw_remain = len;
    ctx->raw_cb     = cb;
    ctx->raw_user   = user;
    return EBADGE_OK;
}

/** Run the frame callback with the in-cb guard up, then honour any raw arm. */
static void dispatch_frame(ebadge_rx_ctx_t *ctx, ebadge_frame_cb_t cb,
                           const uint8_t *params, uint16_t params_len,
                           void *user)
{
    ctx->in_frame_cb = true;
    cb(ctx->hdr[0], ctx->hdr[1], ctx->hdr[2], params, params_len, user);
    ctx->in_frame_cb = false;

    if (ctx->raw_armed)
    {
        /* Keep raw_remain / raw_cb / raw_user; only the arm flag is spent. */
        ctx->raw_armed = false;
        ctx->hdr_got   = 0;
        ctx->body_len  = 0;
        ctx->body_got  = 0;
        ctx->state     = EBADGE_RX_S_RAW;
        EBADGE_LOG1("rx: raw body mode, %u bytes expected",
                    (unsigned)ctx->raw_remain);
        return;
    }
    ebadge_frame_rx_reset(ctx);
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
                    dispatch_frame(ctx, cb, NULL, 0, user);
                    frames_out++;
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
                    dispatch_frame(ctx, cb, ctx->body, ctx->body_len, user);
                    frames_out++;
                }
                break;
            }

        case EBADGE_RX_S_RAW:
            {
                /* Hand the chunk straight out of the feed buffer -- no copy,
                 * no accumulation.  A body larger than one ATT write simply
                 * arrives as several callbacks.                             */
                uint32_t have = (uint32_t)(len - i);
                uint32_t take = (have < ctx->raw_remain) ? have
                                : ctx->raw_remain;
                ctx->raw_remain -= take;
                i = (uint16_t)(i + take);

                ebadge_raw_cb_t rcb   = ctx->raw_cb;
                void           *ruser = ctx->raw_user;
                uint32_t        rem   = ctx->raw_remain;

                /* Resume framing *before* the callback so a sink that turns
                 * around and feeds us (or resets us) sees a sane state.    */
                if (rem == 0)
                {
                    ebadge_frame_rx_reset(ctx);
                    EBADGE_LOG("rx: raw body complete, framing resumed");
                }
                if (rcb)
                {
                    rcb(bytes + (i - take), (uint16_t)take, rem, ruser);
                }
                break;
            }

        default:
            /* Unreachable; treat as corruption and resync. */
            ebadge_frame_rx_reset(ctx);
            break;
        }
    }
    return frames_out;
}
