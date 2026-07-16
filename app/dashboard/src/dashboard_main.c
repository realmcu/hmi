#include "ameba_soc.h"
#include "os_wrapper.h"

/*
 * Copyright (c) 2026 Realtek Semiconductor Corp.
 * All rights reserved.
 *
 * Licensed under the Realtek License, Version 1.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License from Realtek
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "os_wrapper.h"

#if defined(CONFIG_BT) && CONFIG_BT
#include "dashboard_ble.h"
#endif
#if defined(CONFIG_BT_EXT) && CONFIG_BT_EXT
#include "dashboard_bt_ext.h"
#endif
#if defined(CONFIG_WLAN) && CONFIG_WLAN
#include "dashboard_wifi.h"
#include "dashboard_img_rx.h"
#ifdef DASHBOARD_USE_THIRD_PARTY_NAV
#include "dashboard_third_party_nav.h"
#endif
#endif
#include <stdlib.h>
#include <string.h>

#include "ameba_soc.h"
#include "os_wrapper.h"

#include "gui_version.h"
#include "gui_server.h"

#define LOG_TAG "HoneyGUI-Demos"

static void honeygui_print_version(void) {
    RTK_LOGI(LOG_TAG, "===== HoneyGUI Version Information =====\n");
    RTK_LOGI(LOG_TAG, "Version:     %s\n", VERSION_TAG);
    RTK_LOGI(LOG_TAG, "Branch:      %s\n", VERSION_BRANCH);
    RTK_LOGI(LOG_TAG, "Commit:      %s\n", VERSION_COMMIT);
    RTK_LOGI(LOG_TAG, "Build Date:  %s\n", VERSION_BUILD_DATE);
    RTK_LOGI(LOG_TAG, "Repo Status: %s\n", VERSION_REPO_STATUS);
    RTK_LOGI(LOG_TAG, "========================================\n");
}

static void honeygui_task(void *param) {
    UNUSED(param);

    RTK_LOGI(LOG_TAG, "honeygui_task start...\n");

    honeygui_print_version();

    gui_server_init();
    gui_set_keep_active_time(1000000);
    // TODO: Initialize HoneyGUI and run demos here

    RTK_LOGI(LOG_TAG, "honeygui_task end\n");

    rtos_task_delete(NULL);
}

static const char *TAG = "DASHBOARD";

void app_example(void)
{	
	if (rtos_task_create(NULL, "honeygui_task", honeygui_task, NULL, 1024 * 32, 1) != RTK_SUCCESS) {
		RTK_LOGE(TAG, "Failed to create honeygui_task\n");
	}
#if defined(CONFIG_BT) && CONFIG_BT
	if (rtos_task_create(NULL, "ble_task", dash_board_ble_task, NULL, 1024 * 4, 1) != RTK_SUCCESS) {
		RTK_LOGE(TAG, "Failed to create dash_board_ble_task\n");
	}
#endif
#if defined(CONFIG_BT_EXT) && CONFIG_BT_EXT
	if (rtos_task_create(NULL, "bt_ext_task", dash_board_bt_ext_task, NULL, 1024 * 4, 1) != RTK_SUCCESS) {
		RTK_LOGE(TAG, "Failed to create dash_board_bt_ext_task\n");
	}
#endif
#if defined(CONFIG_WLAN) && CONFIG_WLAN
#ifdef DASHBOARD_USE_THIRD_PARTY_NAV
	dashboard_third_party_nav_start();
#else
	if (rtos_task_create(NULL, "wifi_task", dash_board_wifi_task, NULL, 1024 * 4, 1) != RTK_SUCCESS) {
		RTK_LOGE(TAG, "Failed to create dash_board_wifi_task\n");
	}
	if (rtos_task_create(NULL, "img_rx_task", dash_board_img_rx_task, NULL, 1024 * 4, 1) != RTK_SUCCESS) {
		RTK_LOGE(TAG, "Failed to create dash_board_img_rx_task\n");
	}
#endif
#endif
}
