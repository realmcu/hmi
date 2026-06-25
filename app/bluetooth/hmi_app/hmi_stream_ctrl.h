#ifndef _HMI_STREAM_CTRL_H_
#define _HMI_STREAM_CTRL_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "stream_transport.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Initialize the independent stream service:
 *        register the stream GATT service (0xFFD4 RX / 0xFFD5 TX),
 *        create the stream RX queue and stream task.
 *        Call from BLE profile init (after the BT stack is up).
 */
void hmi_stream_ctrl_init(void);

/**
 * @brief Return the shared STP transport that carries reassembled video frames
 *        to the gui_stream widget.
 *
 *        The frame pool and transport are created and owned by the app at GUI
 *        init (easy_demoMain_user.c, via stp_instance_create()); this just
 *        forwards that single shared instance (same handle as
 *        gui_stream_transport_get()) -- borrowed, never created here.  Returns
 *        NULL until the app has created it.  Kept (hmi_l2_stream_*) for
 *        backward compatibility; new code may call gui_stream_transport_get().
 */
stp_transport_t *hmi_l2_stream_get_tp(void);

#ifdef __cplusplus
}
#endif

#endif /* _HMI_STREAM_CTRL_H_ */
