#ifndef _WATCH_L2_CMD_REMOTE_H_
#define _WATCH_L2_CMD_REMOTE_H_

#include "watch_l2_cmd_ids.h"

/* Bidirectional camera control. Keys 0x01-0x0F are requests (Dev -> App),
 * 0x10-0x1F are state facts (App -> Dev), and 0x20-0x2F are reserved for
 * preview pull triggers. The device consumes state; the app is authoritative. */

/* Control requests (Dev -> App) */
#define WATCH_L2_RC_CAPTURE               0x01u   /* vlen=0                             */
#define WATCH_L2_RC_SET_ZOOM              0x04u   /* 4B: zoom_x100 u16be + reserved u16 */

/* State facts (App -> Dev) */
#define WATCH_L2_RC_STATE_REPORT          0x10u   /* TLV (see spec 5.2)                 */
#define WATCH_L2_RC_CTRL_RESULT           0x11u   /* 3B: key + code + detail            */
#define WATCH_L2_RC_LAST_SHOT_READY       0x12u   /* 3B: shot_id u16be + reserved       */

/* CTRL_RESULT error codes */
#define WATCH_L2_RC_ERR_UNSUPPORTED       0x01u
#define WATCH_L2_RC_ERR_BUSY              0x02u
#define WATCH_L2_RC_ERR_OUT_OF_RANGE      0x03u
#define WATCH_L2_RC_ERR_NOT_READY         0x04u
#define WATCH_L2_RC_ERR_UNKNOWN           0xffu

/* STATE_REPORT TLV tags; unknown tags are skipped for forward compatibility. */
#define WATCH_L2_RC_TAG_RECORDING         0x01u   /* 1B  */
#define WATCH_L2_RC_TAG_FACING            0x03u   /* 1B  */
#define WATCH_L2_RC_TAG_ZOOM_X100         0x04u   /* 2B be */
#define WATCH_L2_RC_TAG_HAS_LAST_SHOT     0x0fu   /* 1B  */
#define WATCH_L2_RC_TAG_LAST_SHOT_ID      0x10u   /* 2B be */

#endif /* _WATCH_L2_CMD_REMOTE_H_ */
