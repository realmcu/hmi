/**
 * @file    stream_sink.h
 * @brief   Bridges completed preview frames into the GUI stream transport.
 *
 * This is the piece stream_session.c's frame-sink hook was written for: it
 * takes the (chunk, offset, frame_size, is_last) slices stream_session hands
 * out and copies them into a buffer borrowed from the STP transport the GUI
 * owns (gui_stream_transport_get()), then commits it so the gui_stream widget
 * can decode and draw it.
 *
 * Why a separate file rather than code inside stream_session.c:
 *
 *   - stream_session is protocol.  It knows about §6 framing, timeouts and BLE
 *     notifications, and nothing about how a frame is displayed -- which is why
 *     it exposes a sink instead of calling a decoder.  Keeping the GUI side out
 *     of it is what lets the same state machine drive a different consumer.
 *   - This file is the only place in app/protocol/ that touches the GUI, and
 *     even here the coupling is one header (stream_transport.h -- stdint and
 *     stdbool, no guidef.h) plus one function-local extern for the getter.
 *
 * THREADING.  The sink runs on l2_task, i.e. it is the STP *producer*.  The
 * gui_stream widget is the consumer on gui_task.  That is exactly the SPSC
 * contract stream_transport.h states, so no locking is needed -- but it does
 * mean nothing here may call a consumer-side primitive (stp_acquire_* /
 * stp_release from the consumer's perspective).  The one stp_release() below is
 * on a buffer this side acquired and never published, which never entered the
 * ready queue and so is not shared with the consumer.
 */
#ifndef _EBADGE_STREAM_SINK_H_
#define _EBADGE_STREAM_SINK_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Install the sink on stream_session.  Call once at startup.
 *
 * Must run after stream_session_init() (which clears the sink pointer) and may
 * run before the transport exists: the transport handle is fetched lazily on
 * the first frame, so GUI init order does not constrain this call.
 */
void stream_sink_init(void);

/**
 * @brief  Drop a half-filled frame and return its buffer to the pool.
 *
 * Called from stream_session's reset path.  Without it, a session that ends
 * mid-frame (BLE disconnect, 3s frame gap, App hangs up between slices) leaks
 * one transport buffer per occurrence until the pool is empty and every later
 * frame is dropped at acquire.  The reference implementation in ebadge_8773g
 * has that leak; this does not.
 */
void stream_sink_reset(void);

/** Frames committed to the transport since boot. */
uint32_t stream_sink_frames_shown(void);

/** Frames dropped because no transport buffer was free, or the frame did not
 *  fit the largest size class.  A steady count here against a healthy
 *  jpgs_ingress_frames_ok() means the consumer is not keeping up, or the
 *  phone's frames are larger than APP_STREAM_MAX_FRAME. */
uint32_t stream_sink_frames_dropped(void);

#ifdef __cplusplus
}
#endif

#endif /* _EBADGE_STREAM_SINK_H_ */
