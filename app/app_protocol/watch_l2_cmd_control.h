#ifndef _WATCH_L2_CMD_CONTROL_H_
#define _WATCH_L2_CMD_CONTROL_H_

#include "watch_l2_cmd_ids.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WATCH_L2_CTRL_PHOTO       0x01u
#define WATCH_L2_CTRL_SINGLE_TAP  0x02u
#define WATCH_L2_CTRL_DOUBLE_TAP  0x03u
#define WATCH_L2_CTRL_CAMERA_ST   0x11u

void watch_l2_control_register(void);

#ifdef __cplusplus
}
#endif

#endif /* _WATCH_L2_CMD_CONTROL_H_ */
