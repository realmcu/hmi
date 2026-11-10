#ifndef _HMI_L2_CMD_CONTROL_H_
#define _HMI_L2_CMD_CONTROL_H_

#include "hmi_l2_cmd_ids.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HMI_L2_CTRL_PHOTO       0x01u
#define HMI_L2_CTRL_SINGLE_TAP  0x02u
#define HMI_L2_CTRL_DOUBLE_TAP  0x03u
#define HMI_L2_CTRL_CAMERA_ST   0x11u

void hmi_l2_control_register(void);

#ifdef __cplusplus
}
#endif

#endif /* _HMI_L2_CMD_CONTROL_H_ */
