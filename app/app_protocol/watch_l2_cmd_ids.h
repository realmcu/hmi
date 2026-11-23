#ifndef _WATCH_L2_CMD_IDS_H_
#define _WATCH_L2_CMD_IDS_H_

/* Top-level L2 command IDs owned by the application protocol layer. */
#define WATCH_L2_CMD_OTA          0x01u   /* firmware upgrade */
#define WATCH_L2_CMD_SETTINGS     0x02u   /* device settings */
#define WATCH_L2_CMD_BIND         0x03u   /* bind / login */
#define WATCH_L2_CMD_NOTIFY       0x04u   /* notifications (call, message, etc.) */
#define WATCH_L2_CMD_SPORT        0x05u   /* sport / health data */
#define WATCH_L2_CMD_FACTORY      0x06u   /* factory test */
#define WATCH_L2_CMD_CONTROL      0x07u   /* device control */
#define WATCH_L2_CMD_DUMP_STACK   0x08u   /* dump stack (not yet implemented) */
#define WATCH_L2_CMD_TEST_FLASH   0x09u   /* flash read test (not yet implemented) */
#define WATCH_L2_CMD_LOG          0x0au   /* log transfer */
#define WATCH_L2_CMD_CONN_PARAM   0x0cu   /* BLE connection parameters */
#define WATCH_L2_CMD_WIFI_PROV    0x0du   /* WiFi provisioning */
#define WATCH_L2_CMD_STREAM       0x0eu   /* live video stream */
#define WATCH_L2_CMD_REMOTE       0x0fu   /* remote camera control */
#define WATCH_L2_CMD_FILE_XFER    0x10u   /* file transfer */
#define WATCH_L2_CMD_MODEL        0x12u   /* canonical app-model snapshot */

#endif /* _WATCH_L2_CMD_IDS_H_ */
