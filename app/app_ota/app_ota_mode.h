#ifndef APP_OTA_MODE_H_
#define APP_OTA_MODE_H_

#include <stdbool.h>
#include <stdint.h>

bool app_ota_mode_requested(void);
void app_ota_mode_clear_request(void);

bool app_ota_mode_is_active(void);
void app_ota_mode_set_active(bool active);

void app_ota_mode_request_switch(uint8_t conn_id);
bool app_ota_mode_consume_switch_request(void);
void app_ota_mode_switch_and_reset(void);

#endif /* APP_OTA_MODE_H_ */
