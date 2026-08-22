/**
 * @file    wifi_8711_at_xfer.h
 * @brief   File-transfer control commands: AT+XFERACK / AT+XFERSTOP (v2.2).
 *
 * ---------------------------------------------------------------------------
 * WHY THIS FILE EXISTS: THE 8711 STOPPED DECIDING THE OUTCOME
 * ---------------------------------------------------------------------------
 * Up to protocol v2.1 the 8711 built the EBXR response itself as soon as it had
 * forwarded the last EBFS slot, and closed the TCP connection.  That was wrong
 * in a way that mattered: the 8711 only knows the bytes arrived over Wi-Fi, and
 * "the bytes arrived" is not the result the phone is waiting for.  Whether the
 * whole-file CRC32 matches the BLE offer, and whether the file actually reached
 * flash, are facts only this chip has.
 *
 * v2.2 (sec.6.3 / sec.10.3 / sec.10.4) moved the decision here.  The 8711 now
 * forwards the last slot and *waits*, holding the connection open, for one of:
 *
 *   wifi_8711_at_xfer_ack()   -> AT+XFERACK=<status>,<reason>
 *                                the 8711 emits the 8-byte EBXR carrying these
 *                                two bytes and then closes the TCP.
 *   wifi_8711_at_xfer_stop()  -> AT+XFERSTOP
 *                                immediate shutdown, no EBXR at all.
 *
 * ---------------------------------------------------------------------------
 * THE 120-SECOND BUDGET IS REAL, AND IT IS SHARED
 * ---------------------------------------------------------------------------
 * The 8711 waits at most 120 s (sec.6.3) and then closes on its own WITHOUT an
 * EBXR.  That is the worst outcome available: the phone sees a bare connection
 * close and cannot tell "verify failed" from "device crashed".
 *
 * 120 s is also exactly xfer_session's XS_RECV_IDLE_MS, and the same figure the
 * BLE spec allows for "STA joined -> transfer complete including the ack".  So
 * the whole CRC-compare-plus-flash-commit sequence has to fit inside a window
 * that is already accounted for elsewhere.  fdb_bf_create() pre-erases the
 * reserved range, which for a MiB-scale file is seconds -- worth knowing before
 * anyone adds work between the last slot and the ack.
 *
 * ---------------------------------------------------------------------------
 * WHY EBXR IS NOT PACKED ON THIS SIDE ANY MORE
 * ---------------------------------------------------------------------------
 * There was a version of this where we built the 8 EBXR bytes locally and
 * pushed them through the reserved AT+WLSEND= data tunnel.  Both halves of that
 * are now obsolete: AT+WLSEND= was a reservation the firmware never
 * implemented, and the 8711 builds the EBXR itself from these two arguments.
 * Packing it here as well would mean two encoders for one wire format, and the
 * one on this side would be the one that never gets exercised.
 *
 * ebxr_pack() survives in ebxf_frame.c because a firmware that terminated TCP
 * on this chip would need it.  It has no caller on the 8711 path.
 *
 * ---------------------------------------------------------------------------
 * CONTRACT
 * ---------------------------------------------------------------------------
 * Asynchronous, like every other AT command: 0 means "staged", not "the 8711
 * agreed".  The AT layer is single flight, so -EBUSY is a routine answer and
 * the caller must decide whether to retry -- see xfer_session.c, which owns a
 * small retry because these two commands are the ones that must not be dropped.
 *
 * Callbacks land on the transport thread or the system workqueue, NEVER on
 * l2_task.  Anything touching protocol state must hop with
 * ebadge_task_post_call().
 */
#ifndef _WIFI_8711_AT_XFER_H_
#define _WIFI_8711_AT_XFER_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Status byte of AT+XFERACK, which becomes EBXR's status (eBadge sec.5.3).
 *
 *  Note the polarity: 1 is success here.  This is NOT the BLE result-code
 *  polarity (sec.2.5, where 0x00 is success).  The two are different fields on
 *  different planes and unifying them would corrupt one of them. */
#define WIFI_8711_XFER_STATUS_FAILED   0U
#define WIFI_8711_XFER_STATUS_OK       1U

/**
 * @brief  Outcome of one control command.
 *
 * @param  ok    true if the 8711 answered its "[+XFER...]:OK" line
 * @param  user  the pointer handed to the request
 *
 * ok=false means the command did not take effect -- typically because there was
 * no active file connection (the 8711 answers ERROR), or the transaction timed
 * out.  For an ack that is not recoverable by retrying the ack alone: if the
 * connection is gone, the phone has already lost the TCP result and only the
 * BLE 0x15/0x16 remains, which is why BLE is the authoritative plane.
 */
typedef void (*wifi_8711_at_xfer_done_cb_t)(bool ok, void *user);

/**
 * @brief  Tell the 8711 the business result; it emits EBXR and closes the TCP.
 *
 * @param  status  WIFI_8711_XFER_STATUS_OK / _FAILED
 * @param  reason  eBadge sec.2.6 transfer error code; MUST be 0 when status is
 *                 OK (the spec requires it, and a non-zero reason alongside
 *                 success would leave the App with two contradictory answers)
 *
 * Send this only after length, CRC and any storage commit have all completed --
 * it is the point at which the phone is told the transfer worked.
 *
 * @retval 0         staged
 * @retval -EINVAL   status is OK with a non-zero reason
 * @retval -EBUSY    another AT command is outstanding (single flight)
 * @retval -ENODEV   no 8711 link in this build
 * @retval <0        staging error
 */
int wifi_8711_at_xfer_ack(uint8_t status, uint8_t reason,
                          wifi_8711_at_xfer_done_cb_t cb, void *user);

/**
 * @brief  Abort now: shutdown the socket, stop forwarding, send NO EBXR.
 *
 * For the case where continuing is pointless and the remaining bytes are only
 * costing time -- an EBXF header that disagrees with the BLE offer, a storage
 * failure mid-write, a cancelled session.  Cutting the stream is the whole
 * point: the alternative is letting a file we have already decided to reject
 * clock in slot by slot for as long as the phone keeps sending.
 *
 * Because no EBXR is generated, the App learns the reason from the BLE 0x16
 * XFER_FAIL rather than from TCP.  Callers should emit that.
 *
 * @retval 0 / -EBUSY / -ENODEV / <0   as wifi_8711_at_xfer_ack()
 */
int wifi_8711_at_xfer_stop(wifi_8711_at_xfer_done_cb_t cb, void *user);

/** Counters, for answering "did the ack actually go out?".
 *
 *  Named at_xfer rather than xfer: wifi_8711_xfer.h already owns
 *  wifi_8711_xfer_stats_t for the slot transport's counters, and the two measure
 *  different layers. */
typedef struct
{
    uint32_t acks_ok;       /**< AT+XFERACK with status=1 staged             */
    uint32_t acks_fail;     /**< AT+XFERACK with status=0 staged             */
    uint32_t stops;         /**< AT+XFERSTOP staged                          */
    uint32_t confirmed;     /**< commands the 8711 answered OK to            */
    uint32_t errors;        /**< refused, timed out, or malformed reply      */
} wifi_8711_at_xfer_stats_t;

void wifi_8711_at_xfer_get_stats(wifi_8711_at_xfer_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif /* _WIFI_8711_AT_XFER_H_ */
