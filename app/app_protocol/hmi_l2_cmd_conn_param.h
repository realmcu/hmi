#ifndef _HMI_L2_CMD_CONN_PARAM_H_
#define _HMI_L2_CMD_CONN_PARAM_H_

#include "hmi_l2_cmd_ids.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HMI_L2_CONN_PARAM_REQ       0x01u   /* query request  (phone -> device) */
#define HMI_L2_CONN_PARAM_RSP       0x02u   /* query response (device -> phone) */

void hmi_l2_conn_param_register(void);

#ifdef __cplusplus
}
#endif

#endif /* _HMI_L2_CMD_CONN_PARAM_H_ */
