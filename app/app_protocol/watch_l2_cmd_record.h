#ifndef _WATCH_L2_CMD_RECORD_H_
#define _WATCH_L2_CMD_RECORD_H_

#include <stdbool.h>
#include <stdint.h>

#include "watch_l2_cmd_ids.h"

#define WATCH_L2_RECORD_VERSION       0x00u

#define WATCH_L2_RECORD_KEY_START     0x01u
#define WATCH_L2_RECORD_KEY_DATA      0x02u
#define WATCH_L2_RECORD_KEY_STOP      0x03u
#define WATCH_L2_RECORD_KEY_CANCEL    0x04u
#define WATCH_L2_RECORD_KEY_REQUEST   0x05u

#define WATCH_L2_RECORD_CODEC_OPUS    0x01u
#define WATCH_L2_RECORD_CODEC_PCM     0x02u

#define WATCH_L2_RECORD_STOP_COMPLETED          0x00u
#define WATCH_L2_RECORD_STOP_CANCELLED_BY_PHONE 0x01u
#define WATCH_L2_RECORD_STOP_AUDIO_ERROR        0x02u
#define WATCH_L2_RECORD_STOP_TRANSPORT_ERROR    0x03u
#define WATCH_L2_RECORD_STOP_REQUEST_REJECTED   0x04u

void watch_l2_record_register(void);

#endif /* _WATCH_L2_CMD_RECORD_H_ */
