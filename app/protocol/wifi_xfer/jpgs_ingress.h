/**
 * @file    jpgs_ingress.h
 * @brief   Wi-Fi payload ingress from the 8711 (SPI JPGS slots).
 *
 * The phone's TCP connection terminates on the 8711, not here, so both stored
 * files and preview frames reach this chip as JPGS slots over SPI rather than
 * through ebadge_port_tcp.  This module owns that entry point: it validates and
 * reassembles the slots, then routes the payload to whichever session is live
 * -- xfer_session (opened by 0x10, stores to flash) or stream_session (opened
 * by 0x08, displays).  The wire cannot tell the two apart; the session can.
 *
 * Read the file header of jpgs_ingress.c before changing anything here.  Two
 * rules in particular are easy to break silently: the payload CRC32 lives at
 * offset 28 (offset 12 is ATMC's, a different packet type), and the preview
 * path's tier-2 flow control requires B2W to stay low until the frame has been
 * displayed -- which is a wifi_8711_xfer change, not one that belongs here.
 */
#ifndef _EBADGE_JPGS_INGRESS_H_
#define _EBADGE_JPGS_INGRESS_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Register the JPGS slot sink on the 8711 AT layer.
 *
 * Call once from ebadge_task_init().  Without it, JPGS slots are dropped by the
 * AT layer with no counter -- so "the 8711 is forwarding nothing" and "we are
 * ignoring what it forwards" would look identical.
 */
void jpgs_ingress_init(void);

/**
 * @brief  Drop any frame being reassembled and resync on the next START.
 *
 * For use when a session ends abruptly (BLE disconnect, abort): the 8711 may
 * still be mid-frame, and without this the leftover chunks would be measured
 * against a frame the new session never started.
 */
void jpgs_ingress_reset(void);

/** JPGS slots seen since boot.  Bring-up instrument: non-zero proves the 8711
 *  is forwarding something, independently of whether it validates. */
uint32_t jpgs_ingress_slots(void);

/** Frames reassembled and CRC-checked end to end since boot. */
uint32_t jpgs_ingress_frames_ok(void);

/** Frames abandoned on a bad CRC / illegal or out-of-order chunk (sec.5.2).
 *  Read alongside frames_ok: a healthy link drops none. */
uint32_t jpgs_ingress_frames_dropped(void);

#ifdef __cplusplus
}
#endif

#endif /* _EBADGE_JPGS_INGRESS_H_ */
