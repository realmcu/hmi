#ifndef _HMI_L2_CMD_SETTINGS_H_
#define _HMI_L2_CMD_SETTINGS_H_

#include "hmi_l2_cmd_ids.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HMI_L2_SET_TIME         0x01u
#define HMI_L2_DEVICE_INFO      0x30u
#define HMI_L2_SET_STEP_TARGET  0x05u
#define HMI_L2_SET_USER_PROFILE 0x10u
#define HMI_L2_SET_ANTI_LOST    0x20u
#define HMI_L2_SET_SEDENTARY    0x21u
#define HMI_L2_SET_HANDEDNESS   0x22u
#define HMI_L2_SET_PHONE_OS     0x23u
#define HMI_L2_SET_CALL_LIST    0x24u
#define HMI_L2_SET_CALL_SWITCH  0x25u

void hmi_l2_settings_register(void);

#ifdef __cplusplus
}
#endif

#endif /* _HMI_L2_CMD_SETTINGS_H_ */
