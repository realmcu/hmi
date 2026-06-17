#ifndef __DASHBOARD_KEY_H__
#define __DASHBOARD_KEY_H__

#include "os_wrapper.h"

typedef enum {
	KEY_NEXT_TRACK_PRESS,
	KEY_NEXT_TRACK_RELEASE,
	KEY_PREV_TRACK_PRESS,
	KEY_PREV_TRACK_RELEASE,
	KEY_PLAY_PAUSE_PRESS,
	KEY_PLAY_PAUSE_RELEASE,
} key_event_t;

typedef struct {
	key_event_t event;
} key_msg_t;

/* Send hook: key events are delivered by calling this hook in the producer's
 * context (button IRQ / CLI). The BLE consumer (dashboard_ble or
 * dashboard_bt_ext) registers one and routes the event into its own message
 * loop. */
typedef void (*key_send_hook_t)(key_event_t event);

void key_set_send_hook(key_send_hook_t hook);

void dashboard_button_init(void);

#endif
