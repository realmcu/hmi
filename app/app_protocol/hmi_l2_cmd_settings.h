#ifndef _HMI_L2_CMD_SETTINGS_H_
#define _HMI_L2_CMD_SETTINGS_H_

#include "app_time.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int hmi_l2_decode_time(const uint8_t *value, uint16_t value_len,
                       app_time_local_t *out);
void hmi_l2_settings_register(void);

#ifdef __cplusplus
}
#endif

#endif /* _HMI_L2_CMD_SETTINGS_H_ */
