#ifndef _WATCH_L2_CMD_LOG_H_
#define _WATCH_L2_CMD_LOG_H_

#include "watch_l2_cmd_ids.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WATCH_L2_LOG_OPEN         0x01u
#define WATCH_L2_LOG_CLOSE        0x02u
#define WATCH_L2_LOG_SEND         0x03u

void watch_l2_log_register(void);

#ifdef __cplusplus
}
#endif

#endif /* _WATCH_L2_CMD_LOG_H_ */
