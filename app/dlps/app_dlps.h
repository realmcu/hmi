/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef _APP_DLPS_H_
#define _APP_DLPS_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_DLPS_ENTER_CHECK_TOUCH          0x00000001
#define APP_DLPS_ENTER_CHECK_BUTTON         0x00000002
#define APP_DLPS_ENTER_CHECK_DISPLAY        0x00000004
#define APP_DLPS_ENTER_CHECK_LOCAL_PLAYBACK 0x00000008
#define APP_DLPS_ENTER_CHECK_A2DP           0x00000010
#define APP_DLPS_ENTER_CHECK_USB            0x00000020
#define APP_DLPS_ENTER_CHECK_CONSOLE        0x00000040
#define APP_DLPS_ENTER_CHECK_SPP_CAPTURE    0x00000080
#define APP_DLPS_ENTER_CHECK_DISK           0x00000100
#define APP_DLPS_ENTER_CHECK_INIT           0x00000200
#define APP_DLPS_ENTER_CHECK_ALIPAY         0x00000400
#define APP_DLPS_ENTER_CHECK_WIFI_8711      0x00000800

void app_dlps_enable(uint32_t bit);
void app_dlps_disable(uint32_t bit);
bool app_dlps_check_callback(void);
void app_dlps_enter_callback(void);
void app_dlps_exit_callback(void);
bool app_dlps_check_enter_bits(uint32_t bit);
void app_dlps_init(void);

#ifdef __cplusplus
}
#endif

#endif /* _APP_DLPS_H_ */
