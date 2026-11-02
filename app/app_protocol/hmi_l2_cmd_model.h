#ifndef _HMI_L2_CMD_MODEL_H_
#define _HMI_L2_CMD_MODEL_H_

#include "hmi_l2_cmd_ids.h"

#define HMI_L2_MODEL_GET        0x01u   /* App -> Dev, empty value */
#define HMI_L2_MODEL_SNAPSHOT   0x02u   /* Dev -> App */
#define HMI_L2_MODEL_SCHEMA_V1  0x01u
#define HMI_L2_MODEL_SCHEMA_V2  0x02u   /* v1 + time/activity snapshot */
#define HMI_L2_MODEL_FLAG_BATTERY_VALID 0x01u
#define HMI_L2_MODEL_FLAG_CHARGING      0x02u
#define HMI_L2_MODEL_FLAG_TIME_VALID    0x04u

#endif /* _HMI_L2_CMD_MODEL_H_ */
