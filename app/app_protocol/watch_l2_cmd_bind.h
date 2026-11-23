#ifndef _WATCH_L2_CMD_BIND_H_
#define _WATCH_L2_CMD_BIND_H_

#include "watch_l2_cmd_ids.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WATCH_L2_BIND_REQ         0x01u
#define WATCH_L2_BIND_RSP         0x02u
#define WATCH_L2_LOGIN_REQ        0x03u
#define WATCH_L2_LOGIN_RSP        0x04u
#define WATCH_L2_UNBIND           0x05u
#define WATCH_L2_SUPER_BIND_REQ   0x06u
#define WATCH_L2_SUPER_BIND_RSP   0x07u

void watch_l2_bind_register(void);

#ifdef __cplusplus
}
#endif

#endif /* _WATCH_L2_CMD_BIND_H_ */
