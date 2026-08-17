/**
 * @file    cmd_send_file.c
 * @brief   0x02 SEND_FILE -- BLE small-file direct push (spec §4.2).
 *
 * Unlike every other command, this one is not self-contained: the metadata
 * TLVs arrive in a normal frame, and then TLV_FILE_LENGTH bytes of raw file
 * body follow on the *same* BLE RX stream with no framing of their own.  We
 * ask ebadge_task_expect_raw() to divert exactly that many bytes to
 * on_body(), which runs once per arriving chunk on l2_task.
 *
 * Spec constraints honoured here:
 *   §4.2  reply is 0x04 RESULT with cmd=0x02; no progress notify, no UI
 *   §5.7  while a Wi-Fi transfer is in flight (state != IDLE) -> RESULT BUSY
 *   §8.3  this path is for debug / small config files, never wallpaper
 *
 * CRITICAL: the body is already on the wire by the time we decide anything,
 * so *every* rejection path still has to arm the raw sink and drain those
 * bytes.  Skipping the drain would leave the reassembler interpreting file
 * content as frame headers -- one bad request would desync the link until
 * disconnect.  s_draining tells the sink whether to keep or discard.
 *
 * Storage is deliberately NOT wired up yet: on_body() logs and drops.  See
 * the TODO(app) at the bottom for what landing it entails.
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "../ebadge_cmd.h"
#include "../ebadge_l2.h"
#include "../ebadge_task.h"
#include "../ebadge_errcode.h"
#include "../ebadge_log.h"
#include "../wifi_xfer/xfer_session.h"
#include "../wifi_xfer/stream_session.h"

/*----------------------------------------------------------------------------*
 *  In-flight body state  (l2_task-owned, single-flight -- no locks)
 *----------------------------------------------------------------------------*/
static struct
{
    bool     active;                        /* a body is being routed to us  */
    bool     draining;                      /* discard: request was rejected */
    uint32_t total;                         /* announced TLV_FILE_LENGTH     */
    uint32_t got;                           /* bytes seen so far             */
    uint8_t  file_type;
    char     name[EB_MAX_FILE_NAME + 1];
} s_sf;

/* Copy a utf-8 TLV into a NUL-terminated scratch buffer, truncating at the
 * §2.8 cap.  Returns the number of bytes copied (0 if the TLV is absent).  */
static uint16_t tlv_to_str(const ebadge_tlv_t *tlvs, uint8_t n_tlv,
                           uint8_t type, char *out, uint16_t out_cap)
{
    out[0] = '\0';
    const ebadge_tlv_t *t = ebadge_tlv_find(tlvs, n_tlv, type);
    if (!t || t->len == 0) { return 0; }
    uint16_t n = (t->len < out_cap - 1) ? t->len : (uint16_t)(out_cap - 1);
    memcpy(out, t->val, n);
    out[n] = '\0';
    return n;
}

/*----------------------------------------------------------------------------*
 *  Raw body sink -- one call per RX chunk, in order, on l2_task
 *----------------------------------------------------------------------------*/
static void on_body(const uint8_t *data, uint16_t len, uint32_t remaining,
                    void *user)
{
    (void)user;
    s_sf.got += len;

    if (s_sf.draining)
    {
        /* Rejected up front; RESULT already sent.  Just burn the bytes. */
        if (remaining == 0)
        {
            EBADGE_LOG1("SEND_FILE: drained %u body bytes, discarded",
                        (unsigned)s_sf.got);
            s_sf.active = false;
        }
        return;
    }

    /* First chunk: peek at the head so the log shows the real file magic
     * (JPEG FF D8, PNG 89 50 ...) -- cheapest sanity check during bring-up. */
    if (s_sf.got == len)
    {
        EBADGE_LOG_HEX("SEND_FILE head", data, len);
    }

    EBADGE_LOG3("SEND_FILE: body +%d -> %u/%u", (int)len,
                (unsigned)s_sf.got, (unsigned)s_sf.total);

    /* TODO(app): stream this chunk to storage instead of dropping it.
     * Spec §4.2 lands the file at <FAT_ROOT>/<name>.  The port_storage
     * write-path API (wp_begin / wp_write / wp_commit / wp_abort) already
     * has the right shape -- open on the first chunk, write per chunk,
     * commit when remaining hits 0, abort on disconnect.  Note `data` dies
     * when this returns, so anything asynchronous must copy first.        */

    if (remaining != 0)
    {
        return;                             /* more chunks still coming      */
    }

    /* Body complete.  There is no CRC in §4.2 -- length is the only check,
     * and the reassembler guarantees it by construction, so a short body
     * cannot reach here (it just leaves us waiting for more bytes).        */
    EBADGE_LOG2("SEND_FILE: complete \"%s\" %u bytes (storage hook TODO)",
                s_sf.name, (unsigned)s_sf.got);
    s_sf.active = false;
    (void)ebadge_l2_result_send(EB_CMD_SEND_FILE, EB_RESULT_SUCCEED);
}

/*----------------------------------------------------------------------------*
 *  Reject helper -- answer now, then swallow the body that follows
 *
 *  @p code lets the caller pick between §2.5's FAILED and the V1.3-added
 *  BUSY, which is strictly more informative when we are refusing only
 *  because a Wi-Fi transfer or preview stream owns the storage path.
 *----------------------------------------------------------------------------*/
static void reject_and_drain(uint32_t body_len, uint8_t code, const char *why)
{
    EBADGE_WARN2("SEND_FILE: %s -> RESULT code=0x%02x", why, code);
    (void)ebadge_l2_result_send(EB_CMD_SEND_FILE, code);

    if (body_len == 0)
    {
        return;                             /* nothing to drain             */
    }
    s_sf.active   = true;
    s_sf.draining = true;
    s_sf.total    = body_len;
    s_sf.got      = 0;
    if (ebadge_task_expect_raw(body_len, on_body, NULL) != EBADGE_OK)
    {
        /* Could not arm the sink, so the body will be parsed as frames and
         * the stream is now out of sync.  Nothing we can do from here --
         * the bad-header resync path will chew through it noisily.        */
        EBADGE_ERR1("SEND_FILE: drain arm failed, stream desync of %u bytes",
                    (unsigned)body_len);
        s_sf.active = false;
    }
}

/*----------------------------------------------------------------------------*
 *  Command entry
 *----------------------------------------------------------------------------*/
void handle_send_file(const ebadge_tlv_t *tlvs, uint8_t n_tlv)
{
    char     name[EB_MAX_FILE_NAME + 1];
    char     date[EB_MAX_DATE_STR + 1];
    uint8_t  ftype  = EB_FILE_TYPE_UNKNOWN;
    uint32_t length = 0;

    uint16_t name_len = tlv_to_str(tlvs, n_tlv, EB_TLV_SFILE_NAME,
                                   name, sizeof(name));
    (void)tlv_to_str(tlvs, n_tlv, EB_TLV_SFILE_DATE, date, sizeof(date));
    bool has_type = ebadge_tlv_get_u8(tlvs, n_tlv, EB_TLV_SFILE_TYPE, &ftype);
    bool has_len  = ebadge_tlv_get_u32(tlvs, n_tlv, EB_TLV_SFILE_LENGTH,
                                       &length);

    EBADGE_LOG3("SEND_FILE: name=\"%s\" type=%d len=%u",
                name, (int)ftype, (unsigned)length);
    EBADGE_LOG1("SEND_FILE: date=\"%s\"", date);

    /* A body already in flight means the peer either sent two SEND_FILEs
     * back to back or we lost sync.  Do NOT arm a second drain: the raw
     * sink is single-flight and the first one still owns the stream.      */
    if (s_sf.active)
    {
        EBADGE_WARN1("SEND_FILE: body still in flight (%u left) -> RESULT BUSY",
                     (unsigned)(s_sf.total - s_sf.got));
        (void)ebadge_l2_result_send(EB_CMD_SEND_FILE, EB_RESULT_BUSY);
        return;
    }

    /* Missing TLV_FILE_LENGTH is unrecoverable: without it we cannot know
     * how many body bytes to expect, so we cannot drain either.  Answer
     * FAILED and let the resync path deal with whatever follows.         */
    if (!has_len)
    {
        EBADGE_WARN("SEND_FILE: missing TLV_FILE_LENGTH -> RESULT FAILED");
        (void)ebadge_l2_result_send(EB_CMD_SEND_FILE, EB_RESULT_FAILED);
        return;
    }

    if (length == 0)
    {
        reject_and_drain(0, EB_RESULT_FAILED, "zero-length body");
        return;
    }
    if (length > EB_MAX_SEND_FILE_BYTES)
    {
        reject_and_drain(length, EB_RESULT_FAILED, "body over the small-file cap");
        return;
    }
    if (name_len == 0)
    {
        reject_and_drain(length, EB_RESULT_FAILED, "missing TLV_FILE_NAME");
        return;
    }
    if (has_type && (ftype < EB_FILE_TYPE_JPEG || ftype > EB_FILE_TYPE_MAX))
    {
        reject_and_drain(length, EB_RESULT_FAILED, "unsupported TLV_FILE_TYPE");
        return;
    }
    /* Spec §5.7: a Wi-Fi transfer owns the storage write path while it runs.
     * §2.5's V1.3 BUSY code says "try again later" instead of "no". */
    if (xfer_session_state() != XFER_SESSION_IDLE)
    {
        reject_and_drain(length, EB_RESULT_BUSY, "wifi transfer in progress");
        return;
    }
    /* A preview stream writes nothing, so storage is free -- but it is
     * saturating the radio and the l2_task queue, and a 64 KiB body over BLE
     * would stall it into its own 3s frame timeout.  Refuse instead.       */
    if (stream_session_state() != STREAM_SESSION_IDLE)
    {
        reject_and_drain(length, EB_RESULT_BUSY, "preview stream in progress");
        return;
    }

    /* Accepted -- take the body. */
    s_sf.active    = true;
    s_sf.draining  = false;
    s_sf.total     = length;
    s_sf.got       = 0;
    s_sf.file_type = ftype;
    memcpy(s_sf.name, name, sizeof(s_sf.name));

    if (ebadge_task_expect_raw(length, on_body, NULL) != EBADGE_OK)
    {
        EBADGE_ERR("SEND_FILE: expect_raw failed -> RESULT FAILED");
        s_sf.active = false;
        (void)ebadge_l2_result_send(EB_CMD_SEND_FILE, EB_RESULT_FAILED);
        return;
    }
    /* RESULT is deferred until on_body() sees the last byte -- §4.2 has one
     * reply per command and it has to reflect the whole operation.        */
    EBADGE_LOG1("SEND_FILE: accepted, awaiting %u body bytes",
                (unsigned)length);
}
