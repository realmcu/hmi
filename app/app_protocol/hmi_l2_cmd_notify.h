#ifndef _HMI_L2_CMD_NOTIFY_H_
#define _HMI_L2_CMD_NOTIFY_H_

#include "hmi_l2_cmd_ids.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HMI_L2_CALL_RING        0x01u
#define HMI_L2_CALL_ANSWER      0x02u
#define HMI_L2_CALL_REJECT      0x03u

void hmi_l2_notify_register(void);

#ifdef __cplusplus
}
#endif

#endif /* _HMI_L2_CMD_NOTIFY_H_ */
