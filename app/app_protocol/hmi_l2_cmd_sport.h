#ifndef _HMI_L2_CMD_SPORT_H_
#define _HMI_L2_CMD_SPORT_H_

#include "hmi_l2_cmd_ids.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HMI_L2_SPORT_REQ        0x01u
#define HMI_L2_SPORT_DATA_RSP   0x02u
#define HMI_L2_SLEEP_DATA_RSP   0x03u
#define HMI_L2_SPORT_MORE       0x04u
#define HMI_L2_SLEEP_SET_RSP    0x05u
#define HMI_L2_SPORT_REALTIME   0x06u
#define HMI_L2_SYNC_START       0x07u
#define HMI_L2_SYNC_END         0x08u
#define HMI_L2_TODAY_SPORT_SYNC 0x09u
#define HMI_L2_LAST_SPORT_SYNC  0x0au
#define HMI_L2_CALIBRATE_REQ    0x0bu
#define HMI_L2_CALIBRATE_RSP    0x0cu

/* SPORT L2 Header byte 1: Version=1 in [7:4], Reserve=0 in [3:0].
 * Unlike every other command (which is still version 0), SPORT version 1
 * packets MUST carry 0x10 here -- see spec 3.6 "版本与通用约束". */
#define HMI_L2_SPORT_VER        0x10u

/* Data type carried by SPORT_REQ / SPORT_MORE / SYNC_START / SYNC_END */
#define HMI_L2_SPORT_DT_ACTIVITY 0x01u  /* 15-min activity buckets -> key 0x02 */
#define HMI_L2_SPORT_DT_SLEEP    0x02u  /* sleep state changes     -> key 0x03 */

/* Wire size of one Sport bucket record, and the per-page record cap.
 * A full page is 1 + 8*18 = 145 value bytes (150-byte L2 packet). */
#define HMI_L2_SPORT_REC_SIZE    18u
#define HMI_L2_SPORT_RECS_MAX    8u

void hmi_l2_sport_register(void);

#ifdef __cplusplus
}
#endif

#endif /* _HMI_L2_CMD_SPORT_H_ */
