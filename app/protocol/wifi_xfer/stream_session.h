/**
 * @file    stream_session.h
 * @brief   JPEG preview stream session (BLE 0x08/0x09 + TCP EBXS frames).
 *
 * Sibling of xfer_session, deliberately NOT the same state machine:
 *
 *                     xfer_session (§5)        stream_session (§6)
 *   opened by         0x10 OFFER               0x08 STREAM_OFFER
 *   answered with     0x11 DECISION            0x09 STREAM_DECISION
 *   user dialog       none in V1.3 either      none
 *   states            +WAIT_CONFIRM            no confirm state at all
 *   TCP payload       one 40B EBXF + file      N x (14B EBXS + frame)
 *   per-frame crc     no (one whole-file crc)  yes, every frame
 *   storage           writes a wallpaper       writes NOTHING, ever
 *   ends when         size bytes received      App closes TCP / 3s frame gap
 *   0x14 progress     yes, throttled           no (nothing to total up)
 *
 * The two are mutually exclusive: they both want the SoftAP and TCP port
 * 9000, so whichever is non-IDLE makes the other answer 0x16 / BUSY.  The
 * cross-check is explicit in each offer path.
 *
 * All entry points must run on l2_task context (from a frame handler or via
 * ebadge_task_post_call).  There are no locks.
 */
#ifndef _EBADGE_STREAM_SESSION_H_
#define _EBADGE_STREAM_SESSION_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------------------------------------------------------------*
 *  State  (spec §6.7).  Note there is no WAIT_CONFIRM: §4.5 has the device
 *  decide by itself, so an accepted offer goes straight to WAIT_STA.
 *----------------------------------------------------------------------------*/
typedef enum
{
    STREAM_SESSION_IDLE = 0,
    STREAM_SESSION_WAIT_STA,
    STREAM_SESSION_RECV,
    STREAM_SESSION_COMPLETING,
} stream_session_state_t;

/*----------------------------------------------------------------------------*
 *  Frame sink -- where decoded preview frames go
 *
 *  Frames are delivered CHUNK-WISE, never buffered whole: a 512 KiB cap on
 *  one frame is fine as a sanity limit but not as an allocation.  The sink
 *  gets (chunk, offset within frame, total frame size) and can either stream
 *  into a decoder or accumulate into its own buffer.
 *
 *  @p is_last is true on the chunk that completes the frame.  CRC has NOT
 *  been verified at that point -- it is checked right after the sink returns,
 *  so a sink that renders immediately may render one corrupt frame.  A sink
 *  that cares should accumulate and wait for stream_session_frame_ok().
 *----------------------------------------------------------------------------*/
typedef void (*stream_frame_sink_t)(const uint8_t *chunk, uint16_t len,
                                    uint32_t offset, uint32_t frame_size,
                                    bool is_last, void *user);

/** Install the sink.  Pass NULL to go back to log-only. */
void stream_session_set_frame_sink(stream_frame_sink_t sink, void *user);

/*----------------------------------------------------------------------------*
 *  Lifecycle
 *----------------------------------------------------------------------------*/
/** Register with the ebadge_task tick fanout.  Called once at startup. */
void stream_session_init(void);

/** Current state -- used by the 0x10 / 0x02 paths to detect a busy radio. */
stream_session_state_t stream_session_state(void);

/*----------------------------------------------------------------------------*
 *  Entry points  (all on l2_task)
 *----------------------------------------------------------------------------*/
/**
 * @brief  Handle a decoded 0x08 STREAM_OFFER (spec §4.5).  Decides on its own
 *         -- there is no user prompt in V1.3 -- and always answers 0x09:
 *
 *           busy (either session)   -> 0x09 REJECT, reason BUSY
 *           file_type not a stream  -> 0x09 REJECT, reason FMT_UNSUPPORTED
 *           fps outside our window  -> 0x09 NEGOTIATE with the fps we will do
 *           AP / listener failure   -> 0x09 REJECT, reason AP_START
 *           otherwise               -> 0x09 ACCEPT, then 0x13 AP_INFO
 *
 *         On accept (or negotiate) the SoftAP comes up and TCP starts
 *         listening before 0x13 goes out, so the App can join immediately.
 *
 * @param  name       from TLV_XFER_NAME; informational only (nothing is stored)
 * @param  file_type  from TLV_XFER_TYPE
 * @param  fps        from TLV_XFER_FPS
 */
void stream_session_offer(const char *name, uint8_t file_type, uint8_t fps);

/** SoftAP reports the STA (App) associated.  Arms the §6.5 10s connect timer. */
void stream_session_on_sta_joined(void);

/** TCP delivered bytes.  Handles EBXS header/payload reassembly. */
void stream_session_on_tcp_data(const uint8_t *data, uint16_t len);

/** TCP closed.  For a stream this is the NORMAL end of session (§6.4). */
void stream_session_on_tcp_close(int reason /* ebadge_tcp_close_reason_t */);

/** GAP disconnected, or another session needs the radio -- tear down now. */
void stream_session_abort(void);

/*----------------------------------------------------------------------------*
 *  Introspection
 *----------------------------------------------------------------------------*/
/** Frames whose CRC verified, since the session started. */
uint32_t stream_session_frames_ok(void);
/** Frames dropped on a CRC mismatch, since the session started. */
uint32_t stream_session_frames_bad(void);
/** The frame rate actually agreed with the App (0 when idle). */
uint8_t  stream_session_fps(void);

#ifdef __cplusplus
}
#endif

#endif /* _EBADGE_STREAM_SESSION_H_ */
