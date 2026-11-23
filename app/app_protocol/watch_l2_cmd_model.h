#ifndef _WATCH_L2_CMD_MODEL_H_
#define _WATCH_L2_CMD_MODEL_H_

#include "watch_l2_cmd_ids.h"

#define WATCH_L2_MODEL_GET        0x01u   /* App -> Dev, empty value */
#define WATCH_L2_MODEL_SNAPSHOT   0x02u   /* Dev -> App */
#define WATCH_L2_MODEL_SCHEMA_V1  0x01u
#define WATCH_L2_MODEL_SCHEMA_V2  0x02u   /* v1 + time/activity snapshot */
#define WATCH_L2_MODEL_FLAG_BATTERY_VALID 0x01u
#define WATCH_L2_MODEL_FLAG_CHARGING      0x02u
#define WATCH_L2_MODEL_FLAG_TIME_VALID    0x04u

#endif /* _WATCH_L2_CMD_MODEL_H_ */
