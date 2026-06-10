/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef _WIFI_APP_DOWNLOAD_H_
#define _WIFI_APP_DOWNLOAD_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "wifi_xmodem.h"

/* 一键式固件烧录（阻塞）:
 * GPIO 复位 → MP 模式 → 握手 → Flash 分块读取 → xmodem_send → xmodem_finish */
T_XMODEM_STATUS wifi_download_firmware(void);

#ifdef __cplusplus
}
#endif

#endif /* _WIFI_APP_DOWNLOAD_H_ */
