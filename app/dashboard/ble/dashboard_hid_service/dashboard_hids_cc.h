#ifndef __DASHBOARD_HIDS_CC_H__
#define __DASHBOARD_HIDS_CC_H__

#ifdef __cplusplus
extern "C"  {
#endif

#include <rtk_bt_def.h>
#include <stdbool.h>

#define DASHBOARD_HID_CC_SRV_ID             20

typedef enum {
	DASHBOARD_HID_CC_SCAN_NEXT_TRACK        = 0,
	DASHBOARD_HID_CC_SCAN_PREV_TRACK        = 1,
	DASHBOARD_HID_CC_STOP                   = 2,
	DASHBOARD_HID_CC_PLAY_PAUSE             = 3,
	DASHBOARD_HID_CC_MUTE                   = 4,
	DASHBOARD_HID_CC_VOLUME_UP              = 5,
	DASHBOARD_HID_CC_VOLUME_DOWN            = 6,
} T_DASHBOARD_HID_CC_CONSUMER_KEY;

void dashboard_hid_cc_srv_callback(uint8_t event, void *data);

uint16_t dashboard_hid_cc_srv_add(void);

void dashboard_hid_cc_send_key(uint16_t conn_handle, uint16_t key_bitmap);

void dashboard_hid_cc_prev_track(uint16_t conn_handle, bool press);

void dashboard_hid_cc_next_track(uint16_t conn_handle, bool press);

void dashboard_hid_cc_play_pause(uint16_t conn_handle, bool press);

void dashboard_hid_cc_disconnect(uint16_t conn_handle);

void dashboard_hid_cc_status_deinit(void);

#ifdef __cplusplus
}
#endif

#endif
