#ifndef _HMI_L2_CMD_SPORT_H_
#define _HMI_L2_CMD_SPORT_H_

#include "hmi_l2_cmd_ids.h"

#ifdef __cplusplus
extern "C" {
#endif

/* CMD 0x05, L2 version 1. Wire authority: HoneyProtocol
 * docs/protocol/0x05-data.rst. */
#define HMI_L2_SPORT_VER              0x10u

#define HMI_L2_SPORT_KEY_REQUEST_ALL  0x00u
#define HMI_L2_SPORT_KEY_ACTIVITY_REQ 0x01u
#define HMI_L2_SPORT_KEY_ACTIVITY     0x21u
#define HMI_L2_SPORT_KEY_SLEEP        0x22u
#define HMI_L2_SPORT_KEY_SYNC_START   0x40u
#define HMI_L2_SPORT_KEY_SYNC_END     0x41u
#define HMI_L2_SPORT_KEY_SYNC_ACK     0x42u
#define HMI_L2_SPORT_KEY_TODAY_REQ    0x60u
#define HMI_L2_SPORT_KEY_TODAY_DATA   0x61u

#define HMI_L2_SPORT_REC_SIZE         18u
#define HMI_L2_SPORT_RECS_MAX         8u

void hmi_l2_sport_register(void);

#ifdef __cplusplus
}
#endif

#endif /* _HMI_L2_CMD_SPORT_H_ */
