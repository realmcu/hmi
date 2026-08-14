/**
 * @file    ebadge_frame.h
 * @brief   eBadge V1.2 wire frame codec (5B header, no CRC, no ACK).
 *
 * Frame:
 *
 *   +-----+-----+-------+-----------+-----------+
 *   | ver | cmd | flags | len_lo   | len_hi   |  ...params (up to 512B)
 *   +-----+-----+-------+-----------+-----------+
 *
 * The reassembler is stateful and byte-stream friendly: BLE ATT writes may
 * split a frame across writes or coalesce many frames into one.  Feed bytes
 * with ebadge_frame_feed(); complete frames dispatch via a callback.
 *
 * One command (0x02 SEND_FILE, spec §4.2) is followed by a *raw* body on the
 * same RX stream -- unframed bytes, no header of their own.  A handler asks
 * for that by calling ebadge_frame_expect_raw() from inside its frame
 * callback; the reassembler then routes exactly that many bytes to a sink
 * before returning to normal framing.  See EBADGE_RX_S_RAW below.
 */
#ifndef _EBADGE_FRAME_H_
#define _EBADGE_FRAME_H_

#include <stdint.h>
#include <stdbool.h>
#include "ebadge_cmd.h"          /* EBADGE_HDR_LEN / EBADGE_PARAMS_MAX */

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------------------------------------------------------------*
 *  Encoder (TX)
 *----------------------------------------------------------------------------*/
/**
 * @brief  Pack a command frame into a caller-provided output buffer.
 *
 * @param  cmd        Command id (EB_CMD_*).
 * @param  params     Pointer to params bytes (may be NULL if len == 0).
 * @param  params_len Params length (0..EBADGE_PARAMS_MAX).
 * @param  out        Output buffer.
 * @param  out_cap    Capacity of @p out (must be >= EBADGE_HDR_LEN + params_len).
 * @return >0 total length written; <0 on error (see ebadge_status_t).
 */
int ebadge_frame_pack(uint8_t cmd, const uint8_t *params, uint16_t params_len,
                      uint8_t *out, uint16_t out_cap);

/*----------------------------------------------------------------------------*
 *  Decoder (RX)  -- stateful, byte-stream
 *----------------------------------------------------------------------------*/
typedef enum
{
    EBADGE_RX_S_HDR = 0,     /* accumulating the 5B header             */
    EBADGE_RX_S_BODY,        /* accumulating params_len body bytes     */
    EBADGE_RX_S_RAW,         /* passing through an unframed raw body   */
} ebadge_rx_state_t;

/**
 * @brief  Sink for raw (unframed) body bytes -- see ebadge_frame_expect_raw().
 *
 * Called once per input chunk, in arrival order, straight out of the feed
 * buffer: there is NO accumulation, so @p data dies when the callback
 * returns and a consumer that needs the whole body must copy or stream it.
 *
 * @param  data       Chunk bytes (never NULL, @p len > 0).
 * @param  len        Chunk length.
 * @param  remaining  Bytes still expected after this chunk; 0 means the body
 *                    is complete and framing has resumed.
 * @param  user       Opaque cookie passed to ebadge_frame_expect_raw().
 */
typedef void (*ebadge_raw_cb_t)(const uint8_t *data, uint16_t len,
                                uint32_t remaining, void *user);

typedef struct
{
    ebadge_rx_state_t state;
    uint8_t           hdr[EBADGE_HDR_LEN];
    uint16_t          hdr_got;
    uint16_t          body_len;
    uint16_t          body_got;
    uint8_t           body[EBADGE_PARAMS_MAX];
    /* Raw pass-through, armed by ebadge_frame_expect_raw() ---------------- */
    bool              in_frame_cb; /* true while a frame cb is running       */
    bool              raw_armed;   /* set during a frame cb, consumed after  */
    uint32_t          raw_remain;  /* bytes still to route to raw_cb         */
    ebadge_raw_cb_t   raw_cb;
    void             *raw_user;
} ebadge_rx_ctx_t;

/**
 * @brief  Callback invoked for every complete frame extracted from the stream.
 *         Owned by the caller; body pointer is only valid within the call.
 */
typedef void (*ebadge_frame_cb_t)(uint8_t ver, uint8_t cmd, uint8_t flags,
                                  const uint8_t *params, uint16_t params_len,
                                  void *user);

/** Reset the reassembler (call on connect / on framing error recovery). */
void ebadge_frame_rx_reset(ebadge_rx_ctx_t *ctx);

/**
 * @brief  Feed @p len bytes into the reassembler; @p cb fires per full frame.
 *
 * Multiple complete frames in one call are all dispatched.  On framing error
 * (params_len > EBADGE_PARAMS_MAX or bad ver) the reassembler resets and the
 * remaining bytes are consumed as fresh header candidates.  This is
 * best-effort resync -- callers should not depend on it for correctness;
 * the ATT reliability layer is expected to deliver clean frame boundaries.
 *
 * @return  Number of complete frames dispatched.
 */
int ebadge_frame_feed(ebadge_rx_ctx_t *ctx,
                      const uint8_t *bytes, uint16_t len,
                      ebadge_frame_cb_t cb, void *user);

/**
 * @brief  Route the next @p len stream bytes to @p cb instead of framing them.
 *
 * MUST be called from inside an ebadge_frame_cb_t, i.e. while the frame that
 * announces the body is being handled.  The switch takes effect as soon as
 * that callback returns, so any bytes already queued behind the metadata
 * frame in the same feed() call are picked up correctly.
 *
 * There is no timeout here: a peer that announces a body and then goes quiet
 * leaves the stream in EBADGE_RX_S_RAW.  Recovery is the link layer's job --
 * ebadge_frame_rx_reset() on disconnect discards the partial body.
 *
 * @param  len   Body length, must be > 0.
 * @return 0 on success, negative if not called from a frame callback or if
 *         @p len is 0 (see ebadge_status_t).
 */
int ebadge_frame_expect_raw(ebadge_rx_ctx_t *ctx, uint32_t len,
                            ebadge_raw_cb_t cb, void *user);

#ifdef __cplusplus
}
#endif

#endif /* _EBADGE_FRAME_H_ */
