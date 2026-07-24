#ifndef _HMI_L2_CMD_XFER_H_
#define _HMI_L2_CMD_XFER_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Register the file-transfer L2 command handler and subscribe to
 *         EVT_BLE_DISCONNECTED (so a link loss aborts a half-written file).
 *
 * Called once during app bring-up (see hmi_l2_handlers_register).
 */
void hmi_l2_xfer_register(void);

#ifdef __cplusplus
}
#endif

#endif /* _HMI_L2_CMD_XFER_H_ */
