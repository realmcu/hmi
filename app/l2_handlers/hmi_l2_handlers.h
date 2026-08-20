#ifndef _HMI_L2_HANDLERS_H_
#define _HMI_L2_HANDLERS_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Register all application-layer L2 command handlers.
 *         Must be called during system init, before any BLE data arrives.
 */
void hmi_l2_handlers_register(void);

#ifdef __cplusplus
}
#endif

#endif /* _HMI_L2_HANDLERS_H_ */
