#ifndef _HMI_L2_CMD_STREAM_H_
#define _HMI_L2_CMD_STREAM_H_

#include "hmi_l2_cmd_ids.h"

#define HMI_L2_KS_OPEN              0x01u   /* App -> Dev: open stream session        */
#define HMI_L2_KS_ACK               0x02u   /* Dev -> App: confirm open / error       */
#define HMI_L2_KS_FRAME             0x03u   /* App -> Dev: frame data chunk           */
#define HMI_L2_KS_CLOSE             0x04u   /* App -> Dev: close stream session       */
#define HMI_L2_KS_CREDIT            0x05u   /* Dev -> App: credit grant (flow ctrl)   */
#define HMI_L2_KS_REPORT            0x06u   /* Dev -> App: frame reception status     */

/* KS_OPEN codec field */
#define HMI_L2_KS_CODEC_MSV1        0x00u   /* RGB555 16bpp, MSV1 compatible    */
#define HMI_L2_KS_CODEC_JPEG        0x01u   /* standard JPEG byte stream        */
#define HMI_L2_KS_CODEC_H264        0x02u   /* baselined H264 byte stream       */

/* KS_ACK result field */
#define HMI_L2_KS_ACK_OK            0x00u
#define HMI_L2_KS_ACK_BAD_CODEC     0x01u
#define HMI_L2_KS_ACK_BUSY          0x02u   /* device busy, retry later         */

/* KS_ACK init_credits: device RX buffer depth in KS_FRAME packets;
 * 0xFFFF = no flow control (fire-and-forget fallback). */
#define HMI_L2_KS_CREDITS_UNLIMITED 0xFFFFu

#endif /* _HMI_L2_CMD_STREAM_H_ */
