#ifndef _HMI_L2_CMD_BIND_H_
#define _HMI_L2_CMD_BIND_H_

#include "hmi_l2_cmd_ids.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HMI_L2_BIND_REQ         0x01u
#define HMI_L2_BIND_RSP         0x02u
#define HMI_L2_LOGIN_REQ        0x03u
#define HMI_L2_LOGIN_RSP        0x04u
#define HMI_L2_UNBIND           0x05u
#define HMI_L2_SUPER_BIND_REQ   0x06u
#define HMI_L2_SUPER_BIND_RSP   0x07u

void hmi_l2_bind_register(void);

#ifdef __cplusplus
}
#endif

#endif /* _HMI_L2_CMD_BIND_H_ */
