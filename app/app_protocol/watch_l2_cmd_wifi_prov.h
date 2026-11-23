#ifndef _WATCH_L2_CMD_WIFI_PROV_H_
#define _WATCH_L2_CMD_WIFI_PROV_H_

#include "watch_l2_cmd_ids.h"

#define WATCH_L2_WIFI_CONFIG_SET      0x01u   /* push SSID/password      (phone -> device) */
#define WATCH_L2_WIFI_CONFIG_ACK      0x02u   /* accept / reject         (device -> phone) */
#define WATCH_L2_WIFI_STATUS_REQ      0x03u   /* poll current state      (phone -> device) */
#define WATCH_L2_WIFI_STATUS          0x04u   /* state report / IP:port  (device -> phone) */

/* WIFI_CONFIG_ACK result */
#define WATCH_L2_WIFI_ACK_ACCEPTED    0x00u
#define WATCH_L2_WIFI_ACK_REJECTED    0x01u

/* WIFI_CONFIG_ACK error codes */
#define WATCH_L2_WIFI_ERR_NONE        0x00u
#define WATCH_L2_WIFI_ERR_MALFORMED   0x01u
#define WATCH_L2_WIFI_ERR_UNSUPPORTED 0x02u
#define WATCH_L2_WIFI_ERR_INVALID_SSID 0x03u
#define WATCH_L2_WIFI_ERR_INVALID_PWD 0x04u
#define WATCH_L2_WIFI_ERR_BUSY        0x05u

/* WIFI_STATUS state codes */
#define WATCH_L2_WIFI_STATE_IDLE      0x00u
#define WATCH_L2_WIFI_STATE_CONNECTING 0x01u
#define WATCH_L2_WIFI_STATE_CONNECTED 0x02u
#define WATCH_L2_WIFI_STATE_FAILED    0x03u

/* WIFI_STATUS error codes (state = FAILED) */
#define WATCH_L2_WIFI_STATUS_ERR_NONE    0x00u
#define WATCH_L2_WIFI_STATUS_ERR_AUTH    0x01u   /* wrong SSID / password */
#define WATCH_L2_WIFI_STATUS_ERR_NO_AP   0x02u   /* AP not found */
#define WATCH_L2_WIFI_STATUS_ERR_DHCP    0x03u
#define WATCH_L2_WIFI_STATUS_ERR_TIMEOUT 0x04u
#define WATCH_L2_WIFI_STATUS_ERR_TCP     0x05u   /* TCP server start failed */
#define WATCH_L2_WIFI_STATUS_ERR_UNKNOWN 0x06u

/* WIFI_CONFIG_SET flag bits */
#define WATCH_L2_WIFI_FLAG_SAVE_CRED  0x01u   /* persist credentials on device */

#endif /* _WATCH_L2_CMD_WIFI_PROV_H_ */
