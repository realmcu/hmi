#ifndef _HMI_L2_CMD_REMOTE_H_
#define _HMI_L2_CMD_REMOTE_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Remote-control protocol (CMD 0x0F, spec v2 -- see the hmi-android-apk spec
 * 2026-07-30-remote-control-protocol-design.md).
 *
 * Roles from the device side:
 *   - We SEND control requests (CAPTURE, SET_ZOOM, ...) and DO NOT touch local
 *     UI on send.  App is the authority; we only trust STATE_REPORT.
 *   - We RECEIVE STATE_REPORT / CTRL_RESULT / LAST_SHOT_READY and update the
 *     local mirror of the authoritative state (queried via the getters below).
 *
 * P0 covers CAPTURE + SET_ZOOM; everything else the device sends will come back
 * from app as a CTRL_RESULT UNSUPPORTED (spec section 12).
 */

/* Register the CMD 0x0F handler.  Called from hmi_l2_handlers_register(). */
void hmi_l2_remote_register(void);

/*----------------------------------------------------------------------------*
 * Outbound (Dev -> App)
 *----------------------------------------------------------------------------*/

/* Ask the app to capture a single frame.  Return 0 on send success, <0 on
 * transport error.  Do NOT change any local UI on the strength of this call --
 * wait for LAST_SHOT_READY / STATE_REPORT (spec section 2, principle 1). */
int hmi_l2_remote_send_capture(void);

/* Ask the app to set zoom to zoom_x100 (e.g. 250 = 2.5x).  Per spec section 11
 * P0 we do NOT clamp on the device side -- the app will reject out-of-range
 * with CTRL_RESULT code 0x03. */
int hmi_l2_remote_send_set_zoom(uint16_t zoom_x100);

/*----------------------------------------------------------------------------*
 * Inbound-derived state (UI reads from here; only mutated from STATE_REPORT)
 *----------------------------------------------------------------------------*/

uint16_t hmi_l2_remote_state_zoom_x100(void);      /* default 100 (1.0x)          */
bool     hmi_l2_remote_state_recording(void);      /* default false               */
uint8_t  hmi_l2_remote_state_facing(void);         /* 0=back (default), 1=front   */
bool     hmi_l2_remote_state_has_last_shot(void);  /* default false               */
uint16_t hmi_l2_remote_state_last_shot_id(void);   /* default 0                   */

/*----------------------------------------------------------------------------*
 * Event callbacks (all optional; nullable; single subscriber each)
 *----------------------------------------------------------------------------*/

/* Fired whenever any tracked STATE_REPORT tag changes value. */
typedef void (*hmi_l2_remote_state_cb_t)(void);
void hmi_l2_remote_set_state_cb(hmi_l2_remote_state_cb_t cb);

/* Fired on LAST_SHOT_READY.  Independent of state_cb so the UI can pop a
 * "new photo" hint without decoding TLV. */
typedef void (*hmi_l2_remote_shot_cb_t)(uint16_t shot_id);
void hmi_l2_remote_set_shot_cb(hmi_l2_remote_shot_cb_t cb);

/* Fired on CTRL_RESULT (i.e. the app rejected one of our requests).  Success
 * is signaled implicitly by STATE_REPORT (spec section 5.2). */
typedef void (*hmi_l2_remote_ctrl_result_cb_t)(uint8_t key, uint8_t code);
void hmi_l2_remote_set_ctrl_result_cb(hmi_l2_remote_ctrl_result_cb_t cb);

#ifdef __cplusplus
}
#endif

#endif /* _HMI_L2_CMD_REMOTE_H_ */
