#ifndef _WATCH_L2_CMD_NOTIFY_H_
#define _WATCH_L2_CMD_NOTIFY_H_

#include "watch_l2_cmd_ids.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WATCH_L2_CALL_RING        0x01u
#define WATCH_L2_CALL_ANSWER      0x02u
#define WATCH_L2_CALL_REJECT      0x03u

void watch_l2_notify_register(void);

#ifdef __cplusplus
}
#endif

#endif /* _WATCH_L2_CMD_NOTIFY_H_ */
