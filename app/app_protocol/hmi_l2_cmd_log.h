#ifndef _HMI_L2_CMD_LOG_H_
#define _HMI_L2_CMD_LOG_H_

#include "hmi_l2_cmd_ids.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HMI_L2_LOG_OPEN         0x01u
#define HMI_L2_LOG_CLOSE        0x02u
#define HMI_L2_LOG_SEND         0x03u

void hmi_l2_log_register(void);

#ifdef __cplusplus
}
#endif

#endif /* _HMI_L2_CMD_LOG_H_ */
