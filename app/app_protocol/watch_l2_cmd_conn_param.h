#ifndef _WATCH_L2_CMD_CONN_PARAM_H_
#define _WATCH_L2_CMD_CONN_PARAM_H_

#include "watch_l2_cmd_ids.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WATCH_L2_CONN_PARAM_REQ       0x01u   /* query request  (phone -> device) */
#define WATCH_L2_CONN_PARAM_RSP       0x02u   /* query response (device -> phone) */

void watch_l2_conn_param_register(void);

#ifdef __cplusplus
}
#endif

#endif /* _WATCH_L2_CMD_CONN_PARAM_H_ */
