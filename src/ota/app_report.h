/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef _APP_REPORT_H_
#define _APP_REPORT_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** @defgroup APP_REPORT App Report
  * @brief App Report
  * @{
  */

typedef enum
{
    CMD_PATH_NONE = 0x00,
    CMD_PATH_UART = 0x01,
    CMD_PATH_LE   = 0x02,
    CMD_PATH_SPP  = 0x03,
    CMD_PATH_IAP  = 0x04,

} T_CMD_PATH;

typedef enum
{
    EVENT_ACK                               = 0x0000,

    EVENT_OTA_DEV_INFO                      = 0x0600,
    EVENT_OTA_ACTIVE_BANK_VER               = 0x0601,
    EVENT_OTA_START                         = 0x0602,
    EVENT_OTA_PACKET                        = 0x0603,
    EVENT_OTA_VALID                         = 0x0604,
    EVENT_OTA_BUFFER_CHECK_ENABLE           = 0x0605,
    EVENT_OTA_BUFFER_CHECK                  = 0x0606,
    EVENT_OTA_IMG_INFO                      = 0x0607,
    EVENT_OTA_SECTION_SIZE                  = 0x0608,
    EVENT_OTA_DEV_EXTRA_INFO                = 0x0609,
    EVENT_OTA_PROTOCOL_TYPE                 = 0x060A,
    EVENT_OTA_ACTIVE_ACK                    = 0x060B,
    EVENT_OTA_GET_RELEASE_VER               = 0x060C,
    EVENT_OTA_INACTIVE_BANK_VER             = 0x060D,
    EVENT_OTA_COPY_IMG                      = 0x060E,
    EVENT_OTA_CHECK_SHA256                  = 0x060F,
    EVENT_OTA_ROLESWAP                      = 0x0610,
    EVENT_OTA_CHECK_KEY                     = 0x0611,

    EVENT_TOTAL
} T_EVENT_ID;

/**
    * @brief  report app event by spp/le/uart
    * @param  cmd_path br/le/uart
    * @param  event_id event id
    * @param  app_index BR/LE link index
    * @param  data event data
    * @param  len  event len
    * @return void
    */
void app_report_event(uint8_t cmd_path, uint16_t event_id, uint8_t app_index, uint8_t *data,
                      uint16_t len);

/** End of APP_REPORT
* @}
*/

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _APP_REPORT_H_ */
