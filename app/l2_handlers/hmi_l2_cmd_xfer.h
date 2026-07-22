#ifndef _HMI_L2_CMD_XFER_H_
#define _HMI_L2_CMD_XFER_H_

#ifdef __cplusplus
extern "C" {
#endif

void hmi_l2_xfer_register(void);

/* Reset the receive session on link disconnect: aborts a half-written FlashDB
 * BigFile and clears state so the next transfer isn't rejected with BEGIN_BUSY. */
void hmi_l2_xfer_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* _HMI_L2_CMD_XFER_H_ */
