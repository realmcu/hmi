#ifndef _WATCH_L2_CMD_SETTINGS_H_
#define _WATCH_L2_CMD_SETTINGS_H_

#include "watch_l2_cmd_ids.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WATCH_L2_SET_TIME         0x01u
#define WATCH_L2_DEVICE_INFO      0x30u
#define WATCH_L2_SET_STEP_TARGET  0x05u
#define WATCH_L2_SET_USER_PROFILE 0x10u
#define WATCH_L2_SET_ANTI_LOST    0x20u
#define WATCH_L2_SET_SEDENTARY    0x21u
#define WATCH_L2_SET_HANDEDNESS   0x22u
#define WATCH_L2_SET_PHONE_OS     0x23u
#define WATCH_L2_SET_CALL_LIST    0x24u
#define WATCH_L2_SET_CALL_SWITCH  0x25u

void watch_l2_settings_register(void);

#ifdef __cplusplus
}
#endif

#endif /* _WATCH_L2_CMD_SETTINGS_H_ */
